#include "trampoline.h"
#include <string.h>
#include <errno.h>

#ifndef _WIN32
#include <sys/mman.h>
#endif

/* make page writable, patch bytes, restore permissions */
static int patch_page(void *addr, const uint8_t *bytes, size_t len) {
#ifdef _WIN32
    /* host test on Windows — no mprotect, just memcpy for verification */
    memcpy(addr, bytes, len);
    return 0;
#else
    uintptr_t page = (uintptr_t)addr & ~(4095UL);
    size_t    size = len + ((uintptr_t)addr - page);
    if (mprotect((void *)page, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return -errno;
    memcpy(addr, bytes, len);
    __builtin___clear_cache(addr, (uint8_t *)addr + len);
    mprotect((void *)page, size, PROT_READ | PROT_EXEC);
    return 0;
#endif
}

int tramp_install(trampoline_t *t) {
    size_t  size = t->is_thumb ? TRAMP32_SIZE : TRAMP64_SIZE;
    uint8_t buf[TRAMP64_SIZE];
    memcpy(t->backup, t->target, size);
    if (t->is_thumb)
        tramp_generate_thumb2(buf, t->hook);
    else
        tramp_generate_arm64(buf, t->hook);
    return patch_page(t->target, buf, size);
}

int tramp_uninstall(trampoline_t *t) {
    size_t size = t->is_thumb ? TRAMP32_SIZE : TRAMP64_SIZE;
    return patch_page(t->target, t->backup, size);
}

/*
 * C fallback generators — used when building for host (x86_64 test runner).
 * On ARM64/ARM32 device builds the real ASM versions override these via
 * the linker (ASM object is listed after this TU in the build).
 *
 * I emit the raw instruction bytes directly so the host test can verify
 * the exact encoding without needing an ARM assembler.
 */
#if !defined(__aarch64__)
void tramp_generate_arm64(uint8_t *buf, void *target) {
    /* LDR X17, #8  — 0x58000051 little-endian */
    buf[0] = 0x51; buf[1] = 0x00; buf[2] = 0x00; buf[3] = 0x58;
    /* BR  X17      — 0xD61F0220 little-endian */
    buf[4] = 0x20; buf[5] = 0x02; buf[6] = 0x1F; buf[7] = 0xD6;
    /* 64-bit target address, little-endian */
    uint64_t v = (uint64_t)(uintptr_t)target;
    for (int i = 0; i < 8; i++)
        buf[8 + i] = (uint8_t)(v >> (i * 8));
}
#endif /* !__aarch64__ */

#if !defined(__arm__)
void tramp_generate_thumb2(uint8_t *buf, void *target) {
    /*
     * Thumb-2: LDR.W PC, [PC, #0]
     * Encoding: DF F8 00 F0  (little-endian halfwords: F8DF then F000)
     * Followed by 4-byte target address.
     */
    buf[0] = 0xDF; buf[1] = 0xF8;   /* H1 */
    buf[2] = 0x00; buf[3] = 0xF0;   /* H2 */
    uint32_t addr = (uint32_t)(uintptr_t)target;
    buf[4] = (uint8_t)(addr);
    buf[5] = (uint8_t)(addr >>  8);
    buf[6] = (uint8_t)(addr >> 16);
    buf[7] = (uint8_t)(addr >> 24);
}
#endif /* !__arm__ */
