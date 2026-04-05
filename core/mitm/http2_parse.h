#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * http2_parse -- lightweight HTTP/2 frame parser + HPACK decoder.
 *
 * I don't implement full HPACK compression — I walk static + dynamic
 * header tables to extract :method, :path, :status, content-type,
 * and Host/authority.  Good enough to label ring-buffer frames for
 * the daemon without pulling in a heavy dependency.
 *
 * Usage (called from the daemon / go side via cgo):
 *
 *   http2_ctx_t *ctx = http2_ctx_new();
 *   http2_feed(ctx, SSL_DIR_RX, data, len);   // feed raw SSL plaintext
 *   http2_frame_t *f;
 *   while ((f = http2_next_frame(ctx)) != NULL) {
 *       // f->type, f->stream_id, f->headers_json, f->body, f->body_len
 *       http2_frame_free(f);
 *   }
 *   http2_ctx_free(ctx);
 */

#define HTTP2_MAX_STREAMS  256
#define HTTP2_MAX_HDR_LEN  4096
#define HTTP2_MAX_BODY     (1 << 20)  /* 1 MB reassembly cap */

/* connection preface prefix */
#define HTTP2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"

typedef enum {
    H2_FRAME_DATA          = 0x0,
    H2_FRAME_HEADERS       = 0x1,
    H2_FRAME_PRIORITY      = 0x2,
    H2_FRAME_RST_STREAM    = 0x3,
    H2_FRAME_SETTINGS      = 0x4,
    H2_FRAME_PUSH_PROMISE  = 0x5,
    H2_FRAME_PING          = 0x6,
    H2_FRAME_GOAWAY        = 0x7,
    H2_FRAME_WINDOW_UPDATE = 0x8,
    H2_FRAME_CONTINUATION  = 0x9,
} h2_frame_type_t;

/* a fully reassembled HTTP/2 exchange (headers + body) */
typedef struct {
    uint32_t  stream_id;
    uint8_t   dir;               /* SSL_DIR_TX or SSL_DIR_RX */
    char      method[16];        /* GET, POST, ... */
    char      path[512];         /* :path value */
    char      authority[256];    /* :authority / Host */
    int       status;            /* :status (response only, 0 = request) */
    char      content_type[128];
    uint8_t  *body;              /* heap-allocated, caller must free */
    uint32_t  body_len;
    char     *headers_json;      /* all headers as JSON string, heap-alloc */
} http2_frame_t;

typedef struct http2_ctx_st http2_ctx_t;

http2_ctx_t  *http2_ctx_new(void);
void          http2_ctx_free(http2_ctx_t *ctx);

/*
 * Feed raw SSL plaintext. May produce zero or more completed frames.
 * dir: SSL_DIR_TX or SSL_DIR_RX
 */
void          http2_feed(http2_ctx_t *ctx, uint8_t dir,
                         const uint8_t *data, size_t len);

/* Pop the next completed frame, or NULL if none ready. */
http2_frame_t *http2_next_frame(http2_ctx_t *ctx);

/* Free a frame returned by http2_next_frame(). */
void           http2_frame_free(http2_frame_t *f);

/* Parse HTTP/1.1 response — simpler path for non-h2 traffic */
http2_frame_t *http1_parse_response(const uint8_t *data, size_t len,
                                    const char *host);
