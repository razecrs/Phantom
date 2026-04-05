// ARM64 NEON memory scanner — 16 bytes per cycle
// int neon_scan_u32(const void *mem, size_t len, uint32_t val, size_t *out_offsets, int max_results)
// returns number of matches found

.section .text
.global neon_scan_u32
.type   neon_scan_u32, %function
.align  2

// x0 = mem ptr
// x1 = len (bytes)
// w2 = uint32 search value
// x3 = out_offsets array
// w4 = max_results
// returns w0 = count

neon_scan_u32:
    stp  x19, x20, [sp, #-48]!
    stp  x21, x22, [sp, #16]
    stp  x23, lr,  [sp, #32]

    mov  x19, x0          // base ptr
    mov  x20, x1          // len
    mov  x21, x3          // out_offsets
    mov  w22, w4          // max_results
    mov  w23, #0          // count

    dup  v0.4s, w2        // broadcast val to all 4 lanes of v0

    mov  x0, x19          // current ptr
    mov  x1, x20
.loop:
    cmp  x1, #16
    b.lt .tail
    ld1  {v1.16b}, [x0], #16   // load 16 bytes
    sub  x1, x1, #16
    cmeq v2.4s, v1.4s, v0.4s   // compare all 4 u32 lanes
    umaxv s3, v2.4s             // any match?
    fmov w9, s3
    cbz  w9, .loop              // no match, next 16 bytes

    // found match — check each lane
    mov  x9, x0
    sub  x9, x9, #16            // back to start of this chunk
    umov w10, v2.s[0]; cbz w10, .l1; sub x11, x9, x19; str x11, [x21, x23, lsl #3]; add w23, w23, #1; cmp w23, w22; b.eq .done
.l1:umov w10, v2.s[1]; cbz w10, .l2; sub x11, x9, x19; add x11, x11, #4; str x11, [x21, x23, lsl #3]; add w23, w23, #1; cmp w23, w22; b.eq .done
.l2:umov w10, v2.s[2]; cbz w10, .l3; sub x11, x9, x19; add x11, x11, #8; str x11, [x21, x23, lsl #3]; add w23, w23, #1; cmp w23, w22; b.eq .done
.l3:umov w10, v2.s[3]; cbz w10, .loop; sub x11, x9, x19; add x11, x11, #12; str x11, [x21, x23, lsl #3]; add w23, w23, #1; cmp w23, w22; b.eq .done
    b    .loop
.tail:
    // handle remaining < 16 bytes scalar
    cbz  x1, .done
.ts: cmp  x1, #4; b.lt .done
    ldr  w9, [x0], #4; sub x1, x1, #4
    cmp  w9, w2; b.ne .ts
    sub  x11, x0, x19; sub x11, x11, #4
    str  x11, [x21, x23, lsl #3]; add w23, w23, #1
    cmp  w23, w22; b.eq .done
    b    .ts
.done:
    mov  w0, w23
    ldp  x23, lr,  [sp, #32]
    ldp  x21, x22, [sp, #16]
    ldp  x19, x20, [sp], #48
    ret
