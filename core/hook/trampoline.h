#pragma once
#include <stdint.h>
#include <stddef.h>

/* ARM64: LDR X17, #8 + BR X17 = 12 bytes */
#define TRAMP64_SIZE 12
/* ARM32 Thumb-2: LDR PC, [PC, #0] + .word addr = 8 bytes */
#define TRAMP32_SIZE 8

typedef struct {
    void   *target;       /* original method entry */
    void   *hook;         /* my hook function       */
    uint8_t backup[TRAMP64_SIZE]; /* saved original bytes */
    int     is_thumb;     /* ARM32 Thumb mode flag  */
} trampoline_t;

int  tramp_install(trampoline_t *t);
int  tramp_uninstall(trampoline_t *t);
void tramp_generate_arm64(uint8_t *buf, void *target);
void tramp_generate_thumb2(uint8_t *buf, void *target);
