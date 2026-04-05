#include "ringbuf.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

ringbuf_t *ringbuf_create(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return NULL;
    if (ftruncate(fd, sizeof(ringbuf_t)) < 0) { close(fd); return NULL; }
    ringbuf_t *rb = mmap(NULL, sizeof(ringbuf_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (rb == MAP_FAILED) return NULL;
    memset(rb, 0, sizeof(ringbuf_t));
    return rb;
}

__attribute__((visibility("default")))
ringbuf_t *ringbuf_open(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) return NULL;
    ringbuf_t *rb = mmap(NULL, sizeof(ringbuf_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (rb == MAP_FAILED) return NULL;
    return rb;
}

void ringbuf_destroy(ringbuf_t *rb) {
    munmap(rb, sizeof(ringbuf_t));
}

__attribute__((visibility("default")))
int ringbuf_write(ringbuf_t *rb, const void *data, size_t len) {
    uint64_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);
    if (head - tail + len > RING_CAPACITY) return -1;
    for (size_t i = 0; i < len; i++)
        rb->data[(head + i) & RING_MASK] = ((uint8_t *)data)[i];
    __atomic_store_n(&rb->head, head + len, __ATOMIC_RELEASE);
    return 0;
}

size_t ringbuf_read(ringbuf_t *rb, void *out, size_t max) {
    uint64_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);
    uint64_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);
    size_t avail = (size_t)(head - tail);
    if (avail == 0) return 0;
    size_t len = avail < max ? avail : max;
    for (size_t i = 0; i < len; i++)
        ((uint8_t *)out)[i] = rb->data[(tail + i) & RING_MASK];
    __atomic_store_n(&rb->tail, tail + len, __ATOMIC_RELEASE);
    return len;
}
