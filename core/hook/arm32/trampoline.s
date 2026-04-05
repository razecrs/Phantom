// ARM32 Thumb-2 absolute trampoline generator
// void tramp_generate_thumb2(uint8_t *buf, void *target)
// buf[0..7] = LDR PC, [PC, #0] ; NOP ; .word target

.section .text
.global tramp_generate_thumb2
.type   tramp_generate_thumb2, %function
.thumb
.align  1

tramp_generate_thumb2:
    // r0 = buf, r1 = target address
    // Thumb-2: LDR.W PC, [PC, #0] = F8 DF F0 00
    // Memory: DF F8 00 F0 (little-endian halfwords)
    movw r2, #0xF8DF        // H1
    movt r2, #0xF000        // H2
    str  r2, [r0]
    // target address at [r0, #4]
    str  r1, [r0, #4]
    bx   lr
