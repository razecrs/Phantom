#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <android/log.h>

#define TAG "Phantom/DEX"

/*
 * phantom_dex_dump — in-memory DEX dumper.
 *
 * Strategy: scan every readable memory segment in /proc/self/maps for
 * the DEX magic bytes "dex\n035\0" (dex035) or "dex\n036\0" (dex036).
 * When found, read the uint32 at offset 0x20 (file_size field in the
 * DEX header) to know exactly how many bytes to write out.
 *
 * This catches:
 *   - APK-embedded classes.dex loaded by DexClassLoader
 *   - In-memory decrypted DEX (payload of packers / runtime re-loaders)
 *   - Multi-dex (classes2.dex, classes3.dex, ...)
 *
 * output_dir: path where dump_N.dex files will be written.
 * Returns the number of DEX files dumped, or -1 on error.
 */

/* DEX header magic prefixes */
static const uint8_t DEX_MAGIC_035[] = { 'd','e','x','\n','0','3','5','\0' };
static const uint8_t DEX_MAGIC_036[] = { 'd','e','x','\n','0','3','6','\0' };
static const uint8_t DEX_MAGIC_037[] = { 'd','e','x','\n','0','3','7','\0' };
static const uint8_t DEX_MAGIC_038[] = { 'd','e','x','\n','0','3','8','\0' };
static const uint8_t DEX_MAGIC_039[] = { 'd','e','x','\n','0','3','9','\0' };
/* CDEX (compact DEX used by ART since Android 10) */
static const uint8_t CDEX_MAGIC[]    = { 'c','d','e','x','0','0','1','\0' };

static int is_dex_magic(const uint8_t *p) {
    return memcmp(p, DEX_MAGIC_035, 8) == 0 ||
           memcmp(p, DEX_MAGIC_036, 8) == 0 ||
           memcmp(p, DEX_MAGIC_037, 8) == 0 ||
           memcmp(p, DEX_MAGIC_038, 8) == 0 ||
           memcmp(p, DEX_MAGIC_039, 8) == 0 ||
           memcmp(p, CDEX_MAGIC,    8) == 0;
}

/* DEX header: magic(8) + checksum(4) + sha1(20) + file_size(4) */
#define DEX_HDR_FILE_SIZE_OFF 32

static uint32_t dex_file_size(const uint8_t *hdr, size_t seg_size) {
    if (seg_size < DEX_HDR_FILE_SIZE_OFF + 4) return 0;
    uint32_t sz;
    memcpy(&sz, hdr + DEX_HDR_FILE_SIZE_OFF, 4);
    return sz;
}

typedef struct {
    uintptr_t start;
    uintptr_t end;
} map_range_t;

#define MAX_RANGES 512

static int read_maps(map_range_t *ranges, int max) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    int n = 0;
    while (n < max && fgets(line, sizeof(line), f)) {
        /* we want readable segments: "r" as first perm char */
        char perms[8] = {0};
        uintptr_t s = 0, e = 0;
        if (sscanf(line, "%lx-%lx %4s", &s, &e, perms) != 3) continue;
        if (perms[0] != 'r') continue;
        /* skip vsyscall / vvar / vdso — reading them can fault */
        if (strstr(line, "[vvar]") || strstr(line, "[vsyscall]")) continue;
        ranges[n].start = s;
        ranges[n].end   = e;
        n++;
    }
    fclose(f);
    return n;
}

int phantom_dex_dump(const char *output_dir) {
    if (!output_dir) output_dir = "/data/phantom/dumps";

    /* ensure output directory exists */
    mkdir(output_dir, 0755);

    map_range_t ranges[MAX_RANGES];
    int nranges = read_maps(ranges, MAX_RANGES);
    if (nranges <= 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "failed to read /proc/self/maps");
        return -1;
    }

    int dumped = 0;

    for (int i = 0; i < nranges; i++) {
        const uint8_t *base = (const uint8_t *)ranges[i].start;
        size_t         seg  = (size_t)(ranges[i].end - ranges[i].start);

        if (seg < 112) continue; /* smaller than minimum DEX header */

        /* scan for DEX magic anywhere in this segment */
        size_t scan_limit = seg > (16 * 1024 * 1024) ? (16 * 1024 * 1024) : seg;

        for (size_t off = 0; off + 8 <= scan_limit; off += 4) {
            if (!is_dex_magic(base + off)) continue;

            /* found a DEX magic — validate file_size field */
            uint32_t dex_size = dex_file_size(base + off, seg - off);
            if (dex_size < 112 || dex_size > 256 * 1024 * 1024) {
                off += 4; continue; /* bogus size, keep scanning */
            }
            if (off + dex_size > seg) {
                /* DEX spans beyond this segment — skip (partial) */
                off += 4; continue;
            }

            /* write out */
            char outpath[512];
            snprintf(outpath, sizeof(outpath), "%s/dump_%d.dex", output_dir, dumped);

            int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                __android_log_print(ANDROID_LOG_WARN, TAG,
                    "cannot write %s", outpath);
                off += dex_size; continue;
            }

            ssize_t written = write(fd, base + off, dex_size);
            close(fd);

            if (written == (ssize_t)dex_size) {
                __android_log_print(ANDROID_LOG_INFO, TAG,
                    "dumped DEX[%d]: %u bytes → %s", dumped, dex_size, outpath);
                dumped++;
            } else {
                __android_log_print(ANDROID_LOG_WARN, TAG,
                    "partial write for DEX[%d]", dumped);
                remove(outpath);
            }

            off += dex_size - 4; /* skip past this DEX, -4 because loop adds 4 */
        }
    }

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "DEX dump complete: %d files → %s", dumped, output_dir);
    return dumped;
}
