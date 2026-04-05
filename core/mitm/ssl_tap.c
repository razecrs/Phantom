#include "ssl_tap.h"
#include "intercept_engine.h"
#include "../hook/shadowhook.h"
#include <android/log.h>
#include <dlfcn.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* forward declaration — defined in api_network.c, registered by JS Network.patch() */
extern intercept_ctx_t *phantom_get_intercept_ctx(void);

#define TAG "Phantom/SSLTap"

/* ---- BoringSSL/OpenSSL opaque SSL type ---------------------------------- */
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

/* function pointer types for the originals */
typedef int (*ssl_read_fn)(SSL *ssl, void *buf, int num);
typedef int (*ssl_write_fn)(SSL *ssl, const void *buf, int num);
typedef const char *(*ssl_get_sn_fn)(const SSL *ssl, int type);
typedef int (*ssl_verify_fn)(SSL *ssl, uint8_t *out_alert);

/* ---- original function pointers ----------------------------------------- */
static ssl_read_fn  g_orig_ssl_read  = NULL;
static ssl_write_fn g_orig_ssl_write = NULL;
static ssl_get_sn_fn g_orig_get_sn   = NULL;

/* shadowhook stubs for unhooking */
static void *g_stub_read  = NULL;
static void *g_stub_write = NULL;
static void *g_stub_verify = NULL;

static ringbuf_t *g_rb = NULL;

/* ---- frame writer -------------------------------------------------------- */
void ssl_tap_push(ringbuf_t *rb, ssl_dir_t dir,
                  const char *host, uint16_t host_len,
                  const void *data, uint32_t data_len) {
    if (!rb || !data || data_len == 0) return;

    /* header: magic(4) + dir(1) + host_len(2) + data_len(4) = 11 bytes */
    uint8_t hdr[11];
    uint32_t magic = SSL_TAP_MAGIC;
    memcpy(hdr + 0, &magic,     4);
    hdr[4] = (uint8_t)dir;
    memcpy(hdr + 5, &host_len,  2);
    memcpy(hdr + 7, &data_len,  4);

    ringbuf_write(rb, hdr,  sizeof(hdr));
    if (host_len > 0) ringbuf_write(rb, host, host_len);
    ringbuf_write(rb, data, data_len);
}

/* ---- extract hostname from SSL object ------------------------------------ */
/*
 * SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name) returns the SNI
 * hostname the client sent. Works on BoringSSL and OpenSSL >= 1.0.2.
 * TLSEXT_NAMETYPE_host_name == 0
 */
static void get_host(SSL *ssl, char *out, size_t out_max) {
    out[0] = '\0';
    if (!g_orig_get_sn) return;
    const char *sn = g_orig_get_sn(ssl, 0 /* TLSEXT_NAMETYPE_host_name */);
    if (sn) {
        strncpy(out, sn, out_max - 1);
        out[out_max - 1] = '\0';
    }
}

/* ---- in-process intercept: patch JSON bodies before app sees them ------- */
/*
 * Quick heuristic: look for HTTP/1.1 responses with a JSON body.
 * We scan for "\r\n\r\n" (end of headers) and check Content-Type.
 * Returns a heap-allocated patched body (caller frees) or NULL if unchanged.
 *
 * We also extract the Host header to match url_pattern rules.
 */
static int try_patch_inplace(const char *host, void *buf, int len) {
    if (len <= 0) return 0;

    const char *data  = (const char *)buf;
    intercept_ctx_t *ictx = phantom_get_intercept_ctx();
    if (!ictx || ictx->count == 0) return 0;

    /* find header/body separator */
    const char *hdr_end = NULL;
    for (int i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' &&
            data[i+2] == '\r' && data[i+3] == '\n') {
            hdr_end = data + i + 4;
            break;
        }
    }
    if (!hdr_end) return 0;

    int body_off = (int)(hdr_end - data);
    int body_len = len - body_off;
    if (body_len <= 0) return 0;

    /* only patch JSON bodies */
    if (hdr_end[-4] == '\r') { /* has headers */
        if (!memmem(data, (size_t)(hdr_end - data),
                    "application/json", 16)) {
            /* allow plain "{" or "[" start too — some servers omit content-type */
            char first = hdr_end[0];
            if (first != '{' && first != '[') return 0;
        }
    }

    /* build a "url" from the Host header + request path (best-effort) */
    char url[512] = "";
    const char *host_hdr = (const char *)memmem(data, (size_t)(hdr_end - data),
                                                  "Host:", 5);
    if (host_hdr) {
        host_hdr += 5;
        while (*host_hdr == ' ') host_hdr++;
        int i = 0;
        while (i < (int)sizeof(url) - 1 &&
               host_hdr[i] != '\r' && host_hdr[i] != '\n') {
            url[i] = host_hdr[i];
            i++;
        }
        url[i] = '\0';
    } else if (host[0]) {
        strncpy(url, host, sizeof(url) - 1);
    }

    size_t out_len = 0;
    char  *patched = intercept_apply(ictx, url, hdr_end, (size_t)body_len,
                                     &out_len);
    if (!patched) return 0;

    /* copy patched body back into buf (cap at original body_len to avoid overflow) */
    size_t copy_len = out_len < (size_t)body_len ? out_len : (size_t)body_len;
    memcpy((char *)buf + body_off, patched, copy_len);
    /* zero out any leftover bytes */
    if (copy_len < (size_t)body_len)
        memset((char *)buf + body_off + copy_len, 0,
               (size_t)body_len - copy_len);

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "intercept patched %zu→%zu bytes for %s", (size_t)body_len, out_len, url);
    free(patched);
    return 1;
}

/* ---- SSL_read hook ------------------------------------------------------- */
static int hook_ssl_read(SSL *ssl, void *buf, int num) {
    int ret = g_orig_ssl_read(ssl, buf, num);
    if (ret > 0) {
        char host[256];
        get_host(ssl, host, sizeof(host));

        /* attempt in-process JSON patch before app sees the data */
        try_patch_inplace(host, buf, ret);

        /* mirror (possibly patched) data to daemon ring buffer */
        if (g_rb) {
            ssl_tap_push(g_rb, SSL_DIR_RX,
                         host, (uint16_t)strlen(host),
                         buf,  (uint32_t)ret);
        }
    }
    return ret;
}

/* ---- SSL_write hook ------------------------------------------------------ */
static int hook_ssl_write(SSL *ssl, const void *buf, int num) {
    if (num > 0 && g_rb) {
        char host[256];
        get_host(ssl, host, sizeof(host));
        ssl_tap_push(g_rb, SSL_DIR_TX,
                     host, (uint16_t)strlen(host),
                     buf,  (uint32_t)num);
    }
    return g_orig_ssl_write(ssl, buf, num);
}

/*
 * ---- ssl_crypto_x509_session_verify_cert_chain hook ----------------------
 *
 * Bypass SSL pinning for apps that use BoringSSL's built-in certificate
 * verification. Returning 1 skips the entire chain check including:
 *   - public key pins (HPKP / custom pinning)
 *   - certificate transparency enforcement (Android 14+)
 *   - custom trust anchors
 *
 * Flutter apps, Unity networking, and many NDK-built apps all route
 * through this single symbol. Hooking it catches them all.
 */
static int hook_verify_cert_chain(SSL *ssl, uint8_t *out_alert) {
    (void)ssl; (void)out_alert;
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "cert chain verify bypassed");
    return 1; /* success — certificate accepted */
}

/* ---- init / shutdown ---------------------------------------------------- */

/*
 * Try to hook a symbol in several candidate libraries.
 * BoringSSL is embedded in many apps under different SO names:
 *   libssl.so       — system / standalone
 *   libflutter.so   — Flutter engine
 *   libgame.so      — Unity / custom
 * We walk the list and hook the first hit.
 */
static void *hook_sym_any(const char *sym, void *hook_fn, void **orig,
                          const char **libs, int nlibs) {
    for (int i = 0; i < nlibs; i++) {
        void *stub = shadowhook_hook_sym_name(libs[i], sym, hook_fn, orig);
        if (stub) {
            __android_log_print(ANDROID_LOG_INFO, TAG,
                "hooked %s in %s", sym, libs[i]);
            return stub;
        }
    }
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "hook_sym_any: %s not found in any candidate lib", sym);
    return NULL;
}

int ssl_tap_init(ringbuf_t *rb) {
    g_rb = rb;

    static const char *ssl_libs[] = {
        "libssl.so",
        "libssl_3.so",
        "libflutter.so",
        "libunity.so",
        "libgame.so",
        "libapp.so",
    };
    int nlibs = (int)(sizeof(ssl_libs) / sizeof(ssl_libs[0]));

    /* SSL_read */
    g_stub_read = hook_sym_any("SSL_read",
                               (void *)hook_ssl_read,
                               (void **)&g_orig_ssl_read,
                               ssl_libs, nlibs);

    /* SSL_write */
    g_stub_write = hook_sym_any("SSL_write",
                                (void *)hook_ssl_write,
                                (void **)&g_orig_ssl_write,
                                ssl_libs, nlibs);

    /* cert chain bypass */
    g_stub_verify = hook_sym_any("ssl_crypto_x509_session_verify_cert_chain",
                                 (void *)hook_verify_cert_chain,
                                 NULL,
                                 ssl_libs, nlibs);

    /* resolve SSL_get_servername for hostname extraction (no hook needed) */
    for (int i = 0; i < nlibs; i++) {
        void *handle = dlopen(ssl_libs[i], RTLD_NOLOAD | RTLD_NOW);
        if (!handle) continue;
        g_orig_get_sn = (ssl_get_sn_fn)dlsym(handle, "SSL_get_servername");
        dlclose(handle);
        if (g_orig_get_sn) {
            __android_log_print(ANDROID_LOG_DEBUG, TAG,
                "SSL_get_servername resolved from %s", ssl_libs[i]);
            break;
        }
    }

    int ok = (g_stub_read != NULL && g_stub_write != NULL);
    __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, TAG,
        "ssl_tap_init: read=%s write=%s verify=%s sni=%s",
        g_stub_read   ? "ok" : "FAIL",
        g_stub_write  ? "ok" : "FAIL",
        g_stub_verify ? "ok" : "skip",
        g_orig_get_sn ? "ok" : "skip");
    return ok ? 0 : -1;
}

void ssl_tap_shutdown(void) {
    if (g_stub_read)   { shadowhook_unhook(g_stub_read);   g_stub_read   = NULL; }
    if (g_stub_write)  { shadowhook_unhook(g_stub_write);  g_stub_write  = NULL; }
    if (g_stub_verify) { shadowhook_unhook(g_stub_verify); g_stub_verify = NULL; }
    g_rb = NULL;
    __android_log_print(ANDROID_LOG_INFO, TAG, "ssl_tap unhooked");
}
