// ARM64 absolute trampoline generator
// void tramp_generate_arm64(uint8_t *buf, void *target)
// buf[0..11] = LDR X17, #8 ; BR X17 ; .quad target

.section .text
.global tramp_generate_arm64
.type   tramp_generate_arm64, %function
.align  2

tramp_generate_arm64:
    // x0 = buf, x1 = target address
    // emit: 0x58000051 = LDR X17, #8 (load literal 8 bytes ahead)
    mov  w2, #0x51
    movk w2, #0x5800, lsl #16       // LDR X17, #8
    str  w2, [x0]
    // emit: 0xD61F0220 = BR X17
    mov  w2, #0x0220
    movk w2, #0xD61F, lsl #16       // BR X17
    str  w2, [x0, #4]
    // emit: target address (8 bytes)
    str  x1, [x0, #8]
    ret
