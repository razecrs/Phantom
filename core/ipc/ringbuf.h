#pragma once
#include <stdint.h>
#include <stddef.h>

#define RING_CAPACITY  (1 << 16)   /* 64KB, power of 2 for fast modulo */
#define RING_MASK      (RING_CAPACITY - 1)

typedef struct {
    volatile uint64_t head;
    volatile uint64_t tail;
    uint8_t          data[RING_CAPACITY];
} ringbuf_t;

/* mmap a shared ringbuf between module and app */
ringbuf_t *ringbuf_create(const char *path);
ringbuf_t *ringbuf_open(const char *path);
void       ringbuf_destroy(ringbuf_t *rb);

/* lock-free write — called from hook callbacks (hot path) */
int  ringbuf_write(ringbuf_t *rb, const void *data, size_t len);
/* blocking read — called from app IPC thread */
size_t ringbuf_read(ringbuf_t *rb, void *out, size_t max);
