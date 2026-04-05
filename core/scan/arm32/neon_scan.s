// ARM32 NEON memory scanner stub
.text
.global neon_scan_u32
.type   neon_scan_u32, %function
.syntax unified
.arm

neon_scan_u32:
    mov r0, #0
    bx  lr
