// ARM64 hardware exclusive atomics for ring buffer hot path
// These are called from ringbuf.c for the tightest spin loops

.section .text
.align 2

// uint64_t atomic_claim_slot(uint64_t *head, uint64_t len)
// tries LDXR/STXR loop to atomically add len to *head
// returns old head value (start of claimed slot)
.global phantom_atomic_claim
.type   phantom_atomic_claim, %function
phantom_atomic_claim:
    // x0 = head ptr, x1 = len
.retry:
    ldxr  x2, [x0]          // exclusive load
    add   x3, x2, x1        // new value = old + len
    stxr  w4, x3, [x0]      // try exclusive store
    cbnz  w4, .retry         // failed? another core beat us, retry
    mov   x0, x2             // return old head (claimed slot start)
    ret
