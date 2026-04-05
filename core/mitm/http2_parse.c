#include "http2_parse.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <android/log.h>

#define TAG "Phantom/HTTP2"

/* ---- HPACK static header table (RFC 7541 Appendix A, indices 1-61) ------ */
static const struct { const char *name; const char *value; }
HPACK_STATIC[62] = {
    {NULL, NULL},                           /* index 0 unused */
    {":authority",        ""},              /* 1  */
    {":method",           "GET"},           /* 2  */
    {":method",           "POST"},          /* 3  */
    {":path",             "/"},             /* 4  */
    {":path",             "/index.html"},   /* 5  */
    {":scheme",           "http"},          /* 6  */
    {":scheme",           "https"},         /* 7  */
    {":status",           "200"},           /* 8  */
    {":status",           "204"},           /* 9  */
    {":status",           "206"},           /* 10 */
    {":status",           "304"},           /* 11 */
    {":status",           "400"},           /* 12 */
    {":status",           "404"},           /* 13 */
    {":status",           "500"},           /* 14 */
    {"accept-charset",    ""},              /* 15 */
    {"accept-encoding",   "gzip, deflate"}, /* 16 */
    {"accept-language",   ""},              /* 17 */
    {"accept-ranges",     ""},              /* 18 */
    {"accept",            ""},              /* 19 */
    {"access-control-allow-origin", ""},   /* 20 */
    {"age",               ""},              /* 21 */
    {"allow",             ""},              /* 22 */
    {"authorization",     ""},              /* 23 */
    {"cache-control",     ""},              /* 24 */
    {"content-disposition",""},             /* 25 */
    {"content-encoding",  ""},              /* 26 */
    {"content-language",  ""},              /* 27 */
    {"content-length",    ""},              /* 28 */
    {"content-location",  ""},              /* 29 */
    {"content-range",     ""},              /* 30 */
    {"content-type",      ""},              /* 31 */
    {"cookie",            ""},              /* 32 */
    {"date",              ""},              /* 33 */
    {"etag",              ""},              /* 34 */
    {"expect",            ""},              /* 35 */
    {"expires",           ""},              /* 36 */
    {"from",              ""},              /* 37 */
    {"host",              ""},              /* 38 */
    {"if-match",          ""},              /* 39 */
    {"if-modified-since", ""},              /* 40 */
    {"if-none-match",     ""},              /* 41 */
    {"if-range",          ""},              /* 42 */
    {"if-unmodified-since",""},             /* 43 */
    {"last-modified",     ""},              /* 44 */
    {"link",              ""},              /* 45 */
    {"location",          ""},              /* 46 */
    {"max-forwards",      ""},              /* 47 */
    {"proxy-authenticate",""},              /* 48 */
    {"proxy-authorization",""},             /* 49 */
    {"range",             ""},              /* 50 */
    {"referer",           ""},              /* 51 */
    {"refresh",           ""},              /* 52 */
    {"retry-after",       ""},              /* 53 */
    {"server",            ""},              /* 54 */
    {"set-cookie",        ""},              /* 55 */
    {"strict-transport-security",""},       /* 56 */
    {"transfer-encoding", ""},              /* 57 */
    {"user-agent",        ""},              /* 58 */
    {"vary",              ""},              /* 59 */
    {"via",               ""},              /* 60 */
    {"www-authenticate",  ""},              /* 61 */
};

/* ---- HPACK dynamic table entry ------------------------------------------ */
#define HPACK_DYN_MAX 64
typedef struct {
    char name[256];
    char value[512];
} hpack_entry_t;

typedef struct {
    hpack_entry_t entries[HPACK_DYN_MAX];
    int           count;
} hpack_table_t;

static void hpack_insert(hpack_table_t *t, const char *n, const char *v) {
    if (t->count == HPACK_DYN_MAX) {
        /* evict oldest (last entry) by shifting */
        memmove(&t->entries[1], &t->entries[0],
                sizeof(hpack_entry_t) * (HPACK_DYN_MAX - 1));
        t->count = HPACK_DYN_MAX - 1;
    }
    strncpy(t->entries[0].name,  n, 255);
    strncpy(t->entries[0].value, v, 511);
    t->count++;
}

static const hpack_entry_t *hpack_get(hpack_table_t *t, uint32_t idx) {
    if (idx == 0) return NULL;
    if (idx <= 61) {
        /* static — return pointer to a local so we use a scratch buf */
        static hpack_entry_t scratch;
        if (!HPACK_STATIC[idx].name) return NULL;
        strncpy(scratch.name,  HPACK_STATIC[idx].name,  255);
        strncpy(scratch.value, HPACK_STATIC[idx].value, 511);
        return &scratch;
    }
    uint32_t dyn_idx = idx - 62;
    if ((int)dyn_idx >= t->count) return NULL;
    return &t->entries[dyn_idx];
}

/* ---- HPACK integer decode (RFC 7541 §5.1) ------------------------------- */
static uint32_t hpack_int(const uint8_t *p, const uint8_t *end,
                           int prefix_bits, const uint8_t **next) {
    uint32_t mask = (1u << prefix_bits) - 1;
    uint32_t val  = *p & mask;
    p++;
    if (val < mask) { *next = p; return val; }
    uint32_t m = 0;
    while (p < end) {
        uint8_t b = *p++;
        val += (uint32_t)(b & 0x7f) << m;
        m += 7;
        if (!(b & 0x80)) break;
    }
    *next = p;
    return val;
}

/* ---- HPACK string decode (RFC 7541 §5.2) -------------------------------- */
static size_t hpack_string(const uint8_t *p, const uint8_t *end,
                            char *out, size_t out_max,
                            const uint8_t **next) {
    if (p >= end) { *next = p; return 0; }
    uint8_t huffman = *p >> 7;
    uint32_t slen   = hpack_int(p, end, 7, &p);
    if ((size_t)(end - p) < slen) { *next = end; return 0; }
    (void)huffman; /* Huffman decode omitted — copy raw bytes */
    size_t copy = slen < out_max - 1 ? slen : out_max - 1;
    memcpy(out, p, copy);
    out[copy] = '\0';
    *next = p + slen;
    return copy;
}

/* ---- stream reassembly state -------------------------------------------- */
typedef struct {
    uint32_t  stream_id;
    uint8_t   active;
    uint8_t   dir;
    char      method[16];
    char      path[512];
    char      authority[256];
    int       status;
    char      content_type[128];
    /* header JSON accumulator */
    char      hdrs[HTTP2_MAX_HDR_LEN];
    int       hdr_pos;
    int       hdr_first;
    /* body accumulator */
    uint8_t  *body;
    uint32_t  body_cap;
    uint32_t  body_len;
    uint8_t   end_stream;
} stream_state_t;

/* ---- completed frame queue ----------------------------------------------- */
#define FRAME_QUEUE_CAP 64
typedef struct {
    http2_frame_t *frames[FRAME_QUEUE_CAP];
    int            head;
    int            tail;
    int            count;
} frame_queue_t;

static void fq_push(frame_queue_t *q, http2_frame_t *f) {
    if (q->count >= FRAME_QUEUE_CAP) { http2_frame_free(f); return; }
    q->frames[q->tail] = f;
    q->tail = (q->tail + 1) % FRAME_QUEUE_CAP;
    q->count++;
}

static http2_frame_t *fq_pop(frame_queue_t *q) {
    if (!q->count) return NULL;
    http2_frame_t *f = q->frames[q->head];
    q->head = (q->head + 1) % FRAME_QUEUE_CAP;
    q->count--;
    return f;
}

/* ---- connection input buffer --------------------------------------------- */
#define INBUF_CAP (1 << 17) /* 128KB */

struct http2_ctx_st {
    /* raw input buffer (for both TX and RX — we track separately) */
    uint8_t      ibuf_tx[INBUF_CAP];
    uint32_t     ibuf_tx_len;
    uint8_t      ibuf_rx[INBUF_CAP];
    uint32_t     ibuf_rx_len;

    hpack_table_t hpack_tx;
    hpack_table_t hpack_rx;

    stream_state_t streams[HTTP2_MAX_STREAMS];
    int            stream_count;

    frame_queue_t  ready;
    int            preface_done_tx;
    int            preface_done_rx;
};

/* ---- helpers ------------------------------------------------------------- */
static stream_state_t *get_stream(http2_ctx_t *ctx, uint32_t id, uint8_t dir) {
    for (int i = 0; i < ctx->stream_count; i++) {
        if (ctx->streams[i].stream_id == id && ctx->streams[i].active)
            return &ctx->streams[i];
    }
    /* find a free slot */
    int slot = -1;
    for (int i = 0; i < HTTP2_MAX_STREAMS; i++) {
        if (!ctx->streams[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = id % HTTP2_MAX_STREAMS; /* evict if full */
    stream_state_t *s = &ctx->streams[slot];
    memset(s, 0, sizeof(*s));
    s->stream_id = id;
    s->dir       = dir;
    s->active    = 1;
    s->hdr_first = 1;
    snprintf(s->hdrs, sizeof(s->hdrs), "{");
    s->hdr_pos = 1;
    if (slot >= ctx->stream_count) ctx->stream_count = slot + 1;
    return s;
}

static void stream_add_hdr(stream_state_t *s, const char *name, const char *val) {
    /* update pseudo-headers */
    if (strcmp(name, ":method")    == 0) { strncpy(s->method,    val, 15);  return; }
    if (strcmp(name, ":path")      == 0) { strncpy(s->path,      val, 511); return; }
    if (strcmp(name, ":authority") == 0) { strncpy(s->authority, val, 255); return; }
    if (strcmp(name, "host")       == 0) { strncpy(s->authority, val, 255); return; }
    if (strcmp(name, ":status")    == 0) { s->status = atoi(val);           return; }
    if (strcmp(name, "content-type") == 0) {
        strncpy(s->content_type, val, 127); return;
    }

    /* append to JSON header object */
    int remaining = HTTP2_MAX_HDR_LEN - s->hdr_pos - 4;
    if (remaining < 2) return;
    s->hdr_pos += snprintf(s->hdrs + s->hdr_pos, (size_t)remaining,
                           "%s\"%s\":\"%s\"",
                           s->hdr_first ? "" : ",", name, val);
    s->hdr_first = 0;
}

static void stream_append_body(stream_state_t *s, const uint8_t *d, uint32_t len) {
    if (!len) return;
    if (s->body_len + len > HTTP2_MAX_BODY) return;
    if (s->body_len + len > s->body_cap) {
        uint32_t newcap = (s->body_len + len) * 2;
        if (newcap > HTTP2_MAX_BODY) newcap = HTTP2_MAX_BODY;
        s->body = realloc(s->body, newcap);
        s->body_cap = newcap;
    }
    if (!s->body) return;
    memcpy(s->body + s->body_len, d, len);
    s->body_len += len;
}

static void stream_complete(http2_ctx_t *ctx, stream_state_t *s) {
    /* close header JSON */
    if (s->hdr_pos < HTTP2_MAX_HDR_LEN - 1) {
        s->hdrs[s->hdr_pos++] = '}';
        s->hdrs[s->hdr_pos]   = '\0';
    }

    http2_frame_t *f = calloc(1, sizeof(*f));
    if (!f) { s->active = 0; return; }

    f->stream_id   = s->stream_id;
    f->dir         = s->dir;
    f->status      = s->status;
    strncpy(f->method,       s->method,       15);
    strncpy(f->path,         s->path,         511);
    strncpy(f->authority,    s->authority,    255);
    strncpy(f->content_type, s->content_type, 127);

    if (s->body_len) {
        f->body     = s->body;
        f->body_len = s->body_len;
        s->body     = NULL; /* transfer ownership */
    }
    f->headers_json = strdup(s->hdrs);

    s->active = 0;
    fq_push(&ctx->ready, f);
}

/* ---- HEADERS frame decoder ----------------------------------------------- */
static void decode_headers_block(http2_ctx_t *ctx __attribute__((unused)), stream_state_t *s,
                                  hpack_table_t *ht,
                                  const uint8_t *p, const uint8_t *end) {
    while (p < end) {
        uint8_t first = *p;
        if (first & 0x80) {
            /* indexed header field */
            uint32_t idx = hpack_int(p, end, 7, &p);
            const hpack_entry_t *e = hpack_get(ht, idx);
            if (e) stream_add_hdr(s, e->name, e->value);
        } else if ((first & 0xC0) == 0x40) {
            /* literal with incremental indexing */
            uint32_t idx = hpack_int(p, end, 6, &p);
            char name[256] = {0}, value[512] = {0};
            if (idx == 0) {
                hpack_string(p, end, name, sizeof(name), &p);
            } else {
                const hpack_entry_t *e = hpack_get(ht, idx);
                if (e) strncpy(name, e->name, 255);
            }
            hpack_string(p, end, value, sizeof(value), &p);
            hpack_insert(ht, name, value);
            stream_add_hdr(s, name, value);
        } else if ((first & 0xF0) == 0x00 || (first & 0xF0) == 0x10) {
            /* literal without indexing / never indexed */
            uint32_t idx = hpack_int(p, end, 4, &p);
            char name[256] = {0}, value[512] = {0};
            if (idx == 0) {
                hpack_string(p, end, name, sizeof(name), &p);
            } else {
                const hpack_entry_t *e = hpack_get(ht, idx);
                if (e) strncpy(name, e->name, 255);
            }
            hpack_string(p, end, value, sizeof(value), &p);
            stream_add_hdr(s, name, value);
        } else {
            /* dynamic table size update — skip */
            hpack_int(p, end, 5, &p);
        }
    }
}

/* ---- HTTP/2 frame parser ------------------------------------------------- */
/* HTTP/2 frame header: 3-byte length + 1-byte type + 1-byte flags + 4-byte stream_id */
#define H2_FRAME_HDR 9

static void process_h2_frames(http2_ctx_t *ctx, uint8_t dir) {
    uint8_t  *buf = dir == 0 ? ctx->ibuf_tx : ctx->ibuf_rx;
    uint32_t *len = dir == 0 ? &ctx->ibuf_tx_len : &ctx->ibuf_rx_len;
    hpack_table_t *ht = dir == 0 ? &ctx->hpack_tx : &ctx->hpack_rx;

    /* skip connection preface on TX */
    if (dir == 0) {
        int *done = &ctx->preface_done_tx;
        if (!*done && *len >= 24) {
            if (memcmp(buf, HTTP2_PREFACE, 24) == 0) {
                memmove(buf, buf + 24, *len - 24);
                *len -= 24;
            }
            *done = 1;
        }
    } else {
        ctx->preface_done_rx = 1;
    }

    while (*len >= H2_FRAME_HDR) {
        uint32_t flen = ((uint32_t)buf[0] << 16) |
                        ((uint32_t)buf[1] <<  8) |
                         (uint32_t)buf[2];
        if (*len < H2_FRAME_HDR + flen) break; /* wait for more data */

        uint8_t  ftype  = buf[3];
        uint8_t  fflags = buf[4];
        uint32_t sid    = ((uint32_t)(buf[5] & 0x7f) << 24) |
                          ((uint32_t)buf[6] << 16) |
                          ((uint32_t)buf[7] <<  8) |
                           (uint32_t)buf[8];

        const uint8_t *payload     = buf + H2_FRAME_HDR;
        const uint8_t *payload_end __attribute__((unused)) = payload + flen;

        if (sid > 0) {
            stream_state_t *s = get_stream(ctx, sid, dir);
            if (s) {
                if (ftype == H2_FRAME_HEADERS || ftype == H2_FRAME_CONTINUATION) {
                    const uint8_t *block = payload;
                    size_t         blen  = flen;

                    /* HEADERS may have PADDED and/or PRIORITY flags */
                    if (ftype == H2_FRAME_HEADERS) {
                        if (fflags & 0x08) { /* PADDED */
                            uint8_t pad = *block++;
                            blen -= pad + 1;
                        }
                        if (fflags & 0x20) { /* PRIORITY */
                            block += 5;
                            blen  -= 5;
                        }
                    }
                    decode_headers_block(ctx, s, ht, block, block + blen);
                    if (fflags & 0x01) stream_complete(ctx, s); /* END_STREAM */
                } else if (ftype == H2_FRAME_DATA) {
                    const uint8_t *data = payload;
                    uint32_t       dlen = flen;
                    if (fflags & 0x08) { /* PADDED */
                        uint8_t pad = *data++;
                        dlen -= pad + 1;
                    }
                    stream_append_body(s, data, dlen);
                    if (fflags & 0x01) stream_complete(ctx, s); /* END_STREAM */
                } else if (ftype == H2_FRAME_RST_STREAM) {
                    s->active = 0;
                }
            }
        }

        /* advance past this frame */
        uint32_t total = H2_FRAME_HDR + flen;
        memmove(buf, buf + total, *len - total);
        *len -= total;
    }
}

/* ---- public API ---------------------------------------------------------- */

http2_ctx_t *http2_ctx_new(void) {
    http2_ctx_t *ctx = calloc(1, sizeof(*ctx));
    return ctx;
}

void http2_ctx_free(http2_ctx_t *ctx) {
    if (!ctx) return;
    /* drain queue */
    http2_frame_t *f;
    while ((f = fq_pop(&ctx->ready)) != NULL) http2_frame_free(f);
    /* free stream bodies */
    for (int i = 0; i < ctx->stream_count; i++) {
        if (ctx->streams[i].body) free(ctx->streams[i].body);
    }
    free(ctx);
}

void http2_feed(http2_ctx_t *ctx, uint8_t dir,
                const uint8_t *data, size_t len) {
    if (!ctx || !data || !len) return;

    uint8_t  *ibuf = dir == 0 ? ctx->ibuf_tx : ctx->ibuf_rx;
    uint32_t *ilen = dir == 0 ? &ctx->ibuf_tx_len : &ctx->ibuf_rx_len;

    /* append to input buffer */
    size_t copy = len;
    if (*ilen + copy > INBUF_CAP) copy = INBUF_CAP - *ilen;
    if (!copy) return;
    memcpy(ibuf + *ilen, data, copy);
    *ilen += (uint32_t)copy;

    process_h2_frames(ctx, dir);
}

http2_frame_t *http2_next_frame(http2_ctx_t *ctx) {
    return fq_pop(&ctx->ready);
}

void http2_frame_free(http2_frame_t *f) {
    if (!f) return;
    free(f->body);
    free(f->headers_json);
    free(f);
}

/* ---- HTTP/1.1 response parser -------------------------------------------- */
http2_frame_t *http1_parse_response(const uint8_t *data, size_t len,
                                    const char *host) {
    if (!data || len < 12) return NULL;

    /* must start with "HTTP/" */
    if (memcmp(data, "HTTP/", 5) != 0) return NULL;

    http2_frame_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;

    f->dir = 1; /* RX */
    strncpy(f->authority, host ? host : "", 255);

    /* status code */
    const char *p = (const char *)data;
    const char *sp = memchr(p, ' ', len < 16 ? len : 16);
    if (sp) f->status = atoi(sp + 1);

    /* find blank line separating headers from body */
    const char *hdr_end = NULL;
    for (size_t i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' &&
            data[i+2] == '\r' && data[i+3] == '\n') {
            hdr_end = (const char *)data + i + 4;
            break;
        }
    }

    /* build minimal headers JSON */
    char hj[256];
    snprintf(hj, sizeof(hj), "{\"status\":\"%d\"}", f->status);
    f->headers_json = strdup(hj);

    /* find Content-Type */
    const char *ct = NULL;
    {
        const char *scan = (const char *)data;
        size_t remaining = len;
        while (remaining > 13) {
            if (strncasecmp(scan, "content-type:", 13) == 0) {
                ct = scan + 13;
                break;
            }
            scan++; remaining--;
        }
        if (ct) {
            while (*ct == ' ') ct++;
            size_t ctlen = 0;
            while (ct[ctlen] && ct[ctlen] != '\r' && ct[ctlen] != '\n') ctlen++;
            if (ctlen > 127) ctlen = 127;
            memcpy(f->content_type, ct, ctlen);
            f->content_type[ctlen] = '\0';
        }
    }

    /* body */
    if (hdr_end) {
        size_t body_len = len - (size_t)(hdr_end - (const char *)data);
        if (body_len > 0 && body_len <= HTTP2_MAX_BODY) {
            f->body = malloc(body_len + 1);
            if (f->body) {
                memcpy(f->body, hdr_end, body_len);
                f->body[body_len] = 0;
                f->body_len = (uint32_t)body_len;
            }
        }
    }

    return f;
}
