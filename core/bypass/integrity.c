#include "../hook/shadowhook.h"
#include <string.h>
#include <stdlib.h>
#include <android/log.h>

#define TAG "Phantom/Integrity"

/*
 * Native integrity bypass — patches libc/libcutils property reads so
 * the target app sees a clean, locked-bootloader profile even on a
 * rooted device.
 *
 * Functions hooked:
 *   __system_property_get(name, value)   — legacy API used by most apps
 *   __system_property_find(name)         — returns prop_info* used in newer path
 *   __system_property_read_callback()    — called with the prop_info*
 *
 * The Java-layer bypass (scripts/bypass/integrity.js) handles
 * SystemProperties.get() calls inside the Dalvik/ART layer.
 * This C layer catches direct libc calls from NDK code and Unity/Flutter.
 */

typedef int  (*prop_get_fn)(const char *name, char *value);
typedef const void *(*prop_find_fn)(const char *name);
typedef void (*prop_read_cb_fn)(const void *pi,
                                 void (*cb)(void *, const char *, const char *, uint32_t),
                                 void *cookie);

static prop_get_fn        g_orig_prop_get      = NULL;
static prop_find_fn       g_orig_prop_find     = NULL;
static prop_read_cb_fn    g_orig_prop_read_cb  = NULL;

static void *g_stub_get     = NULL;
static void *g_stub_find    = NULL;
static void *g_stub_read_cb = NULL;

/* table of properties to spoof */
static const struct { const char *key; const char *value; } SPOOF[] = {
    { "ro.build.tags",               "release-keys"  },
    { "ro.build.type",               "user"          },
    { "ro.debuggable",               "0"             },
    { "ro.secure",                   "1"             },
    { "ro.boot.verifiedbootstate",   "green"         },
    { "ro.boot.flash.locked",        "1"             },
    { "ro.boot.vbmeta.device_state", "locked"        },
    { "ro.boot.veritymode",          "enforcing"     },
    { "ro.boot.warranty_bit",        "0"             },
    { "ro.warranty_bit",             "0"             },
    { "sys.oem_unlock_allowed",      "0"             },
    { NULL, NULL },
};

static const char *spoof_value(const char *name) {
    for (int i = 0; SPOOF[i].key; i++) {
        if (strcmp(SPOOF[i].key, name) == 0) return SPOOF[i].value;
    }
    return NULL;
}

/* ── __system_property_get hook ─────────────────────────────────────── */
static int hook_prop_get(const char *name, char *value) {
    const char *sv = spoof_value(name);
    if (sv) {
        strncpy(value, sv, 92); /* PROP_VALUE_MAX = 92 */
        value[91] = '\0';
        __android_log_print(ANDROID_LOG_DEBUG, TAG,
            "prop_get('%s') → '%s'", name, sv);
        return (int)strlen(value);
    }
    return g_orig_prop_get(name, value);
}

/* ── __system_property_read_callback hook ────────────────────────────── */
/*
 * Two-pass approach: pass 1 captures the property name via a module-level
 * static callback, pass 2 calls the app's callback with the spoofed value.
 */

/* Thread-local capture state for two-pass property read */
static __thread char  t_captured_name[96];
static __thread char  t_captured_value[96];

static void capture_name_cb(void *cookie,
                              const char *name, const char *value,
                              uint32_t serial) {
    (void)cookie; (void)serial;
    strncpy(t_captured_name,  name  ? name  : "", 95);
    strncpy(t_captured_value, value ? value : "", 95);
}

typedef struct {
    void (*real_cb)(void *, const char *, const char *, uint32_t);
    void  *real_cookie;
    char   spoof_name[96];
    char   spoof_value[96];
} cb_shim_t;

static void spoof_cb(void *cookie,
                      const char *name, const char *value, uint32_t serial) {
    cb_shim_t *shim = (cb_shim_t *)cookie;
    const char *out = shim->spoof_value[0] ? shim->spoof_value : value;
    shim->real_cb(shim->real_cookie, name, out, serial);
    free(shim);
}

static void hook_prop_read_cb(const void *pi,
                               void (*cb)(void *, const char *, const char *, uint32_t),
                               void *cookie) {
    /* pass 1: capture the property name */
    t_captured_name[0] = '\0';
    g_orig_prop_read_cb(pi, capture_name_cb, NULL);

    const char *sv = spoof_value(t_captured_name);
    if (!sv) {
        /* not a spoofed property — call original normally */
        g_orig_prop_read_cb(pi, cb, cookie);
        return;
    }

    __android_log_print(ANDROID_LOG_DEBUG, TAG,
        "prop_read_cb('%s') → '%s'", t_captured_name, sv);

    /* pass 2: call the app's callback with spoofed value */
    cb_shim_t *shim = malloc(sizeof(cb_shim_t));
    if (!shim) { g_orig_prop_read_cb(pi, cb, cookie); return; }
    shim->real_cb     = cb;
    shim->real_cookie = cookie;
    strncpy(shim->spoof_name,  t_captured_name, 95);
    strncpy(shim->spoof_value, sv, 95);
    g_orig_prop_read_cb(pi, spoof_cb, shim);
}

/* ── init ─────────────────────────────────────────────────────────────── */
void phantom_integrity_init(void) {
    g_stub_get = shadowhook_hook_sym_name(
        "libc.so", "__system_property_get",
        (void *)hook_prop_get, (void **)&g_orig_prop_get);

    /* read_callback is optional — some older APIs don't have it */
    g_stub_read_cb = shadowhook_hook_sym_name(
        "libc.so", "__system_property_read_callback",
        (void *)hook_prop_read_cb, (void **)&g_orig_prop_read_cb);

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "integrity_init: prop_get=%s prop_read_cb=%s",
        g_stub_get    ? "ok" : "FAIL",
        g_stub_read_cb ? "ok" : "skip");
}
