#pragma once
#include "../ipc/ringbuf.h"
#include <stdint.h>
#include <stddef.h>

/*
 * ssl_tap -- hooks SSL_read / SSL_write in libssl.so (BoringSSL / OpenSSL).
 *
 * Frame layout written to the ring buffer:
 *
 *   [4] magic   = 0x504D4954  "PMIT"
 *   [1] dir     = 0 (TX / request) | 1 (RX / response)
 *   [2] host_len
 *   [4] data_len
 *   [host_len]  hostname (no NUL)
 *   [data_len]  raw plaintext bytes (HTTP/1.1 or HTTP/2)
 *
 * The Go daemon reads frames, splits HTTP/1.1 or HTTP/2, and
 * feeds JSON bodies to phantom_scan_json() and the UI.
 */

#define SSL_TAP_MAGIC  0x504D4954u

typedef enum {
    SSL_DIR_TX = 0,   /* client -> server (request)  */
    SSL_DIR_RX = 1,   /* server -> client (response) */
} ssl_dir_t;

/* install hooks; rb is the shared ring buffer to write frames into */
int  ssl_tap_init(ringbuf_t *rb);
void ssl_tap_shutdown(void);

/* called by intercept engine to push a modified response frame */
void ssl_tap_push(ringbuf_t *rb, ssl_dir_t dir,
                  const char *host, uint16_t host_len,
                  const void *data, uint32_t data_len);
