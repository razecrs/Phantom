#include "../hook/shadowhook.h"
#include <android/log.h>
#include <string.h>

#define TAG "Phantom/SSLUnpin"

/*
 * ssl_unpin.c — supplemental SSL pinning bypass for native code paths
 * not covered by ssl_tap.c's cert-chain hook.
 *
 * ssl_tap.c handles:
 *   ssl_crypto_x509_session_verify_cert_chain → return 1 (bypass whole chain)
 *
 * This file handles additional bypass points:
 *   SSL_CTX_set_verify        — apps that set VERIFY_PEER flags explicitly
 *   X509_verify_cert          — OpenSSL 1.x path (some vendored libssl)
 *
 * Both are low-risk stubs: if the symbol isn't present the hook silently
 * fails (shadowhook returns NULL, which we ignore).
 */

typedef void (*ssl_ctx_set_verify_fn)(void *ctx, int mode, void *cb);
typedef int  (*x509_verify_cert_fn)(void *store_ctx);

static void  *g_stub_set_verify  = NULL;
static void  *g_stub_x509_verify = NULL;

static ssl_ctx_set_verify_fn  g_orig_set_verify  = NULL;
static x509_verify_cert_fn    g_orig_x509_verify  = NULL;

/* ── SSL_CTX_set_verify — strip VERIFY_PEER flag ──────────────────────────
 * Apps that build their own SSLContext sometimes call
 * SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL).
 * We intercept and replace with SSL_VERIFY_NONE (0) so pinning callbacks
 * are never installed in the first place.
 */
static void hook_ssl_ctx_set_verify(void *ctx, int mode, void *cb) {
    if (mode != 0) {
        __android_log_print(ANDROID_LOG_DEBUG, TAG,
            "SSL_CTX_set_verify mode=%d → 0 (VERIFY_NONE)", mode);
        mode = 0;
    }
    if (g_orig_set_verify)
        g_orig_set_verify(ctx, mode, cb);
}

/* ── X509_verify_cert — always succeed ────────────────────────────────────
 * Older NDK-embedded OpenSSL paths (e.g. apps shipping OpenSSL 1.0.x).
 * Returning 1 = verified OK.
 */
static int hook_x509_verify_cert(void *store_ctx) {
    __android_log_print(ANDROID_LOG_DEBUG, TAG,
        "X509_verify_cert → 1 (bypassed)");
    (void)store_ctx;
    return 1;
}

static const char *SSL_LIBS[] = {
    "libssl.so", "libssl_3.so", "libflutter.so", "libunity.so",
    "libgame.so", "libapp.so", NULL,
};

static void hook_in_any(const char *sym, void *hook, void **orig, void **stub) {
    for (int i = 0; SSL_LIBS[i]; i++) {
        *stub = shadowhook_hook_sym_name(SSL_LIBS[i], sym, hook, orig);
        if (*stub) {
            __android_log_print(ANDROID_LOG_DEBUG, TAG,
                "hooked %s in %s", sym, SSL_LIBS[i]);
            return;
        }
    }
}

void phantom_ssl_unpin_init(void) {
    hook_in_any("SSL_CTX_set_verify",
                (void *)hook_ssl_ctx_set_verify,
                (void **)&g_orig_set_verify,
                &g_stub_set_verify);

    hook_in_any("X509_verify_cert",
                (void *)hook_x509_verify_cert,
                (void **)&g_orig_x509_verify,
                &g_stub_x509_verify);

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "ssl_unpin_init: set_verify=%s x509_verify=%s",
        g_stub_set_verify  ? "ok" : "skip",
        g_stub_x509_verify ? "ok" : "skip");
}
