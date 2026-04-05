#include "../hook/shadowhook.h"
#include <android/log.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TAG "Phantom/RootHide"

/* ── paths to hide from the target app ──────────────────────────────────── */
static const char *ROOT_PATHS[] = {
    /* su binaries */
    "/su", "/su/bin", "/su/bin/su",
    "/sbin/su", "/system/bin/su", "/system/xbin/su",
    "/system/xbin/which",
    /* Magisk */
    "/sbin/magisk", "/sbin/.magisk",
    "/data/adb/magisk", "/data/adb/modules", "/data/adb/modules_update",
    "/data/adb/ksu",
    "/cache/magisk.log",
    /* common root apps */
    "/system/app/Superuser.apk", "/system/app/SuperSU.apk",
    /* KernelSU */
    "/data/adb/ksud",
    /* local tmp */
    "/data/local/tmp/su", "/data/local/tmp/busybox",
    NULL,
};

static int is_root_path(const char *path) {
    if (!path) return 0;
    for (int i = 0; ROOT_PATHS[i]; i++) {
        if (strcmp(path, ROOT_PATHS[i]) == 0) return 1;
    }
    /* hide Phantom's own runtime dirs so the target app can't detect us */
    if (strncmp(path, "/dev/phantom/", 13) == 0)  return 1;
    if (strncmp(path, "/data/phantom/", 14) == 0) return 1;
    return 0;
}

/* ── hook stubs ─────────────────────────────────────────────────────────── */
typedef int  (*access_fn)(const char *path, int mode);
typedef int  (*stat_fn)(const char *path, struct stat *buf);
typedef int  (*stat64_fn)(const char *path, struct stat64 *buf);
typedef int  (*lstat_fn)(const char *path, struct stat *buf);
typedef int  (*open_fn)(const char *path, int flags, ...);
typedef int  (*openat_fn)(int dirfd, const char *path, int flags, ...);
typedef int  (*fstatat_fn)(int dirfd, const char *path, struct stat *buf, int flags);

static access_fn  g_orig_access  = NULL;
static stat_fn    g_orig_stat    = NULL;
static stat64_fn  g_orig_stat64  = NULL;
static lstat_fn   g_orig_lstat   = NULL;
static open_fn    g_orig_open    = NULL;
static openat_fn  g_orig_openat  = NULL;
static fstatat_fn g_orig_fstatat = NULL;

static void *g_stubs[7] = {0};

/* ── hook implementations ─────────────────────────────────────────────────
 * For every function that probes a path: if it's a root path → pretend
 * it doesn't exist (ENOENT) and return the error code the caller expects.
 */

static int hook_access(const char *path, int mode) {
    if (is_root_path(path)) {
        __android_log_print(ANDROID_LOG_DEBUG, TAG,
            "access('%s') → ENOENT", path);
        errno = ENOENT;
        return -1;
    }
    return g_orig_access(path, mode);
}

static int hook_stat(const char *path, struct stat *buf) {
    if (is_root_path(path)) {
        errno = ENOENT;
        return -1;
    }
    return g_orig_stat(path, buf);
}

static int hook_stat64(const char *path, struct stat64 *buf) {
    if (is_root_path(path)) {
        errno = ENOENT;
        return -1;
    }
    return g_orig_stat64(path, buf);
}

static int hook_lstat(const char *path, struct stat *buf) {
    if (is_root_path(path)) {
        errno = ENOENT;
        return -1;
    }
    return g_orig_lstat(path, buf);
}

static int hook_open(const char *path, int flags, ...) {
    if (is_root_path(path)) {
        __android_log_print(ANDROID_LOG_DEBUG, TAG,
            "open('%s') → ENOENT", path);
        errno = ENOENT;
        return -1;
    }
    /* forward without varargs — mode only matters for O_CREAT */
    return g_orig_open(path, flags);
}

static int hook_openat(int dirfd, const char *path, int flags, ...) {
    if (path && is_root_path(path)) {
        errno = ENOENT;
        return -1;
    }
    return g_orig_openat(dirfd, path, flags);
}

static int hook_fstatat(int dirfd, const char *path,
                         struct stat *buf, int flags) {
    if (path && is_root_path(path)) {
        errno = ENOENT;
        return -1;
    }
    return g_orig_fstatat(dirfd, path, buf, flags);
}

/* ── init / shutdown ────────────────────────────────────────────────────── */
void phantom_root_hide_init(void) {
    static const struct {
        const char *sym;
        void       *hook;
        void      **orig;
        int         stub_idx;
    } hooks[] = {
        { "access",   (void*)hook_access,   (void**)&g_orig_access,  0 },
        { "stat",     (void*)hook_stat,     (void**)&g_orig_stat,    1 },
        { "stat64",   (void*)hook_stat64,   (void**)&g_orig_stat64,  2 },
        { "lstat",    (void*)hook_lstat,    (void**)&g_orig_lstat,   3 },
        { "open",     (void*)hook_open,     (void**)&g_orig_open,    4 },
        { "openat",   (void*)hook_openat,   (void**)&g_orig_openat,  5 },
        { "fstatat",  (void*)hook_fstatat,  (void**)&g_orig_fstatat, 6 },
    };

    int ok = 0;
    for (int i = 0; i < (int)(sizeof(hooks)/sizeof(hooks[0])); i++) {
        void *stub = shadowhook_hook_sym_name(
            "libc.so", hooks[i].sym, hooks[i].hook, hooks[i].orig);
        g_stubs[hooks[i].stub_idx] = stub;
        if (stub) ok++;
    }

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "root_hide_init: %d/%d hooks installed",
        ok, (int)(sizeof(hooks)/sizeof(hooks[0])));
}

void phantom_root_hide_shutdown(void) {
    for (int i = 0; i < 7; i++) {
        if (g_stubs[i]) {
            shadowhook_unhook(g_stubs[i]);
            g_stubs[i] = NULL;
        }
    }
    __android_log_print(ANDROID_LOG_INFO, TAG, "root_hide unhooked");
}
