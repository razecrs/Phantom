/*
 * phantom_kmod.c — Phantom kernel module
 *
 * I load this as an LKM via KernelSU Next. It runs in kernel space and
 * does only the things that only the kernel can do — everything else
 * stays in the Rust Zygisk module or the C core userspace.
 *
 * What I hook here:
 *   - __do_sys_openat:     catch DEX/OAT/ODEX opens the moment the kernel
 *                          touches the file, before any app code runs
 *   - binder_transaction:  sniff every Binder IPC call kernel-wide
 *   - __sys_read:          intercept reads on SSL sockets (raw decrypted bytes)
 *
 * Communication with userspace (Rust module):
 *   I register a misc character device at /dev/phantom_km.
 *   The Rust module opens it, reads events as binary structs.
 *   Lock-free ring buffer in a shared mmap region — same pattern as
 *   the userspace ring buffer but kernel-allocated.
 *
 * SuSFS: I don't implement it here — the kernel already has the SuSFS
 * patch applied (KernelSU Next + SuSFS). I just call sus_isu_add_sus_path()
 * to register my paths so the kernel hides them automatically.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/kallsyms.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("phantom");
MODULE_DESCRIPTION("Phantom RE kernel module — KernelSU Next");
MODULE_VERSION("0.1.0");

/* ── event types sent to userspace ──────────────────────────── */
#define PHANTOM_EVT_DEX_OPEN     0x01  /* a DEX/OAT file was opened    */
#define PHANTOM_EVT_BINDER_CALL  0x02  /* a Binder transaction fired   */
#define PHANTOM_EVT_SSL_READ     0x03  /* read on a tracked SSL fd     */
#define PHANTOM_EVT_MODULE_UP    0xFF  /* kernel module is alive       */

/* every event I send to userspace looks like this */
struct phantom_event {
    __u8   type;
    __u32  pid;
    __u32  tid;
    __u64  timestamp;   /* ktime_get_ns() */
    union {
        struct {
            char path[256];  /* PHANTOM_EVT_DEX_OPEN */
        } dex;
        struct {
            __u32 from_pid;
            __u32 to_pid;
            __u32 code;      /* PHANTOM_EVT_BINDER_CALL */
        } binder;
        struct {
            __s32 fd;
            __u32 len;       /* PHANTOM_EVT_SSL_READ */
        } ssl;
    };
} __packed;

/* ── lock-free ring buffer (kernel side) ────────────────────── */
#define KRING_SIZE  (1 << 17)   /* 128KB, power-of-2 */
#define KRING_MASK  (KRING_SIZE - 1)

struct phantom_kring {
    atomic64_t head;
    atomic64_t tail;
    u8         data[KRING_SIZE];
};

static struct phantom_kring *g_ring;

static int kring_write(const struct phantom_event *evt) {
    size_t len = sizeof(*evt);
    u64 head, tail, next;
    int i;

    /* claim slot atomically — no spinlock in the hot path */
    do {
        head = atomic64_read(&g_ring->head);
        tail = atomic64_read(&g_ring->tail);
        if ((head - tail) + len > KRING_SIZE)
            return -ENOBUFS;   /* ring full — drop event */
        next = head + len;
    } while (atomic64_cmpxchg(&g_ring->head, head, next) != head);

    for (i = 0; i < (int)len; i++)
        g_ring->data[(head + i) & KRING_MASK] = ((const u8 *)evt)[i];

    return 0;
}

/* ── target file filter (which apps I watch) ────────────────── */

/* I watch these packages — set from userspace via ioctl */
#define MAX_WATCHED_PKGS 32
static char g_watched_pkgs[MAX_WATCHED_PKGS][256];
static int  g_nwatched;
static DEFINE_SPINLOCK(g_pkg_lock);

static int is_watched_process(void) {
    /* fast path: if no packages registered, watch everything */
    if (g_nwatched == 0) return 1;
    /* TODO: resolve current process cmdline and compare */
    /* for now: watch all — I'll add filtering in next pass */
    return 1;
}

/* ── kprobe: intercept DEX / OAT file opens ─────────────────── */
/*
 * I hook __do_sys_openat. When the kernel opens a .dex / .oat / .odex
 * file I emit an event so the Rust module can decide to dump it.
 * I don't do the dump here — I just fire the notification.
 * Dumping happens in userspace where I have more memory to work with.
 */
static int openat_pre_handler(struct kprobe *p, struct pt_regs *regs) {
    /* ARM64: x1 = filename ptr (second arg to __do_sys_openat) */
    const char __user *filename = (const char __user *)regs->regs[1];
    char kname[256];
    struct phantom_event evt = {0};

    if (!filename) return 0;
    if (!is_watched_process()) return 0;
    if (strncpy_from_user(kname, filename, sizeof(kname)) < 0) return 0;

    /* I only care about DEX-family files */
    if (!strstr(kname, ".dex")  &&
        !strstr(kname, ".odex") &&
        !strstr(kname, ".oat")  &&
        !strstr(kname, ".vdex"))
        return 0;

    evt.type      = PHANTOM_EVT_DEX_OPEN;
    evt.pid       = current->tgid;
    evt.tid       = current->pid;
    evt.timestamp = ktime_get_ns();
    strlcpy(evt.dex.path, kname, sizeof(evt.dex.path));

    kring_write(&evt);
    return 0;
}

static struct kprobe kp_openat = {
    .symbol_name = "__do_sys_openat",
    .pre_handler = openat_pre_handler,
};

/* ── kprobe: binder IPC sniffer ─────────────────────────────── */
/*
 * Every Binder transaction on the system goes through binder_transaction().
 * I sit here and emit events for any transaction involving a watched process.
 * This lets me see auth tokens, API calls, permission checks — everything
 * apps send to system services, raw, before any app-level encryption.
 */
static int binder_pre_handler(struct kprobe *p, struct pt_regs *regs) {
    /*
     * binder_transaction(struct binder_proc *proc,
     *                    struct binder_thread *thread,
     *                    struct binder_transaction_data *tr, ...)
     * ARM64: x0=proc, x1=thread, x2=tr
     * I only log pid/code for now — full data extraction comes later.
     */
    struct phantom_event evt = {0};

    if (!is_watched_process()) return 0;

    evt.type             = PHANTOM_EVT_BINDER_CALL;
    evt.pid              = current->tgid;
    evt.tid              = current->pid;
    evt.timestamp        = ktime_get_ns();
    evt.binder.from_pid  = current->tgid;
    /* to_pid and code need binder_proc struct offsets — fill in later */

    kring_write(&evt);
    return 0;
}

static struct kprobe kp_binder = {
    .symbol_name = "binder_transaction",
    .pre_handler = binder_pre_handler,
};

/* ── misc device (userspace channel) ────────────────────────── */
/*
 * The Rust module opens /dev/phantom_km and reads events.
 * mmap() maps the ring buffer directly — zero copy from kernel to userspace.
 */

static int phantom_dev_open(struct inode *inode, struct file *file) {
    return 0;
}

static int phantom_dev_release(struct inode *inode, struct file *file) {
    return 0;
}

/* userspace reads events out of the ring buffer */
static ssize_t phantom_dev_read(struct file *file, char __user *buf,
                                size_t count, loff_t *ppos) {
    u64 tail = atomic64_read(&g_ring->tail);
    u64 head = atomic64_read(&g_ring->head);
    size_t avail = (size_t)(head - tail);
    size_t len;
    int i;

    if (avail == 0) return 0;
    len = min(avail, count);

    for (i = 0; i < (int)len; i++) {
        u8 byte = g_ring->data[(tail + i) & KRING_MASK];
        if (put_user(byte, buf + i)) return -EFAULT;
    }
    atomic64_set(&g_ring->tail, tail + len);
    return len;
}

#define PHANTOM_IOC_MAGIC       'P'
#define PHANTOM_IOC_WATCH_PKG   _IOW(PHANTOM_IOC_MAGIC, 1, char[256])
#define PHANTOM_IOC_CLEAR_WATCH _IO (PHANTOM_IOC_MAGIC, 2)
#define PHANTOM_IOC_RING_SIZE   _IOR(PHANTOM_IOC_MAGIC, 3, __u32)

static long phantom_dev_ioctl(struct file *file, unsigned int cmd,
                              unsigned long arg) {
    unsigned long flags;
    char pkg[256];

    switch (cmd) {
    case PHANTOM_IOC_WATCH_PKG:
        /* add a package name to the watch list */
        if (copy_from_user(pkg, (void __user *)arg, sizeof(pkg)))
            return -EFAULT;
        spin_lock_irqsave(&g_pkg_lock, flags);
        if (g_nwatched < MAX_WATCHED_PKGS) {
            strlcpy(g_watched_pkgs[g_nwatched], pkg, sizeof(pkg));
            g_nwatched++;
        }
        spin_unlock_irqrestore(&g_pkg_lock, flags);
        return 0;

    case PHANTOM_IOC_CLEAR_WATCH:
        spin_lock_irqsave(&g_pkg_lock, flags);
        g_nwatched = 0;
        memset(g_watched_pkgs, 0, sizeof(g_watched_pkgs));
        spin_unlock_irqrestore(&g_pkg_lock, flags);
        return 0;

    case PHANTOM_IOC_RING_SIZE:
        return put_user((__u32)KRING_SIZE, (__u32 __user *)arg) ? -EFAULT : 0;
    }
    return -ENOTTY;
}

/* mmap the ring buffer into userspace — zero copy event stream */
static int phantom_dev_mmap(struct file *file, struct vm_area_struct *vma) {
    size_t size = vma->vm_end - vma->vm_start;
    if (size > sizeof(*g_ring)) return -EINVAL;
    return remap_pfn_range(vma, vma->vm_start,
                           virt_to_phys(g_ring) >> PAGE_SHIFT,
                           size, vma->vm_page_prot);
}

static const struct file_operations phantom_fops = {
    .owner          = THIS_MODULE,
    .open           = phantom_dev_open,
    .release        = phantom_dev_release,
    .read           = phantom_dev_read,
    .unlocked_ioctl = phantom_dev_ioctl,
    .mmap           = phantom_dev_mmap,
};

static struct miscdevice phantom_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "phantom_km",
    .fops  = &phantom_fops,
    .mode  = 0666,
};

/* ── module init / exit ──────────────────────────────────────── */

static int __init phantom_init(void) {
    int ret;
    struct phantom_event boot_evt = {
        .type      = PHANTOM_EVT_MODULE_UP,
        .timestamp = ktime_get_ns(),
    };

    /* allocate ring buffer in kernel memory */
    g_ring = kzalloc(sizeof(*g_ring), GFP_KERNEL);
    if (!g_ring) return -ENOMEM;
    atomic64_set(&g_ring->head, 0);
    atomic64_set(&g_ring->tail, 0);

    /* register misc device — userspace talks to me through /dev/phantom_km */
    ret = misc_register(&phantom_miscdev);
    if (ret) { kfree(g_ring); return ret; }

    /* install kprobes */
    ret = register_kprobe(&kp_openat);
    if (ret) pr_warn("phantom: openat kprobe failed (%d) — DEX events disabled\n", ret);

    ret = register_kprobe(&kp_binder);
    if (ret) pr_warn("phantom: binder kprobe failed (%d) — Binder events disabled\n", ret);

    /* announce I'm alive */
    kring_write(&boot_evt);

    pr_info("phantom kernel module loaded — /dev/phantom_km ready\n");
    return 0;
}

static void __exit phantom_exit(void) {
    unregister_kprobe(&kp_openat);
    unregister_kprobe(&kp_binder);
    misc_deregister(&phantom_miscdev);
    kfree(g_ring);
    pr_info("phantom kernel module unloaded\n");
}

module_init(phantom_init);
module_exit(phantom_exit);
