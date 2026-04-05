#include "runtime.h"
#include "quickjs/quickjs.h"
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <android/log.h>

#define TAG "Phantom/Script"

/* ── ph.log / ph.warn / ph.error ─────────────────────────────────────────
 *
 * Available to every QuickJS script as:
 *   ph.log("message")
 *   ph.warn("message")
 *   ph.error("message")
 *   ph.tag("MyTag").log("message")   // not implemented yet, left as future
 *
 * All output goes to Android logcat under the tag "Phantom/Script".
 * The host-side `ph log` command reads logcat filtered to this tag.
 */

static JSValue js_ph_log_level(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv, int level) {
    (void)this_val;
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            __android_log_print(level, TAG, "%s", str);
            JS_FreeCString(ctx, str);
        }
    }
    return JS_UNDEFINED;
}

static JSValue js_ph_log(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv) {
    return js_ph_log_level(ctx, this_val, argc, argv, ANDROID_LOG_INFO);
}

static JSValue js_ph_warn(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv) {
    return js_ph_log_level(ctx, this_val, argc, argv, ANDROID_LOG_WARN);
}

static JSValue js_ph_error(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv) {
    return js_ph_log_level(ctx, this_val, argc, argv, ANDROID_LOG_ERROR);
}

/* ph.sleep(ms) — blocking sleep, for simple sequencing in scripts */
static JSValue js_ph_sleep(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1) return JS_UNDEFINED;
    int32_t ms = 0;
    JS_ToInt32(ctx, &ms, argv[0]);
    if (ms > 0) {
        struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    return JS_UNDEFINED;
}

/* ph.env() — returns the package name this script is running in */
static JSValue js_ph_env(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    /* read /proc/self/cmdline to get the package name */
    char cmdline[256] = {0};
    FILE *f = fopen("/proc/self/cmdline", "r");
    if (f) {
        fread(cmdline, 1, sizeof(cmdline) - 1, f);
        fclose(f);
    }
    return JS_NewString(ctx, cmdline[0] ? cmdline : "unknown");
}

/* ── ph.dumpDex(dir?) ────────────────────────────────────────────────────
 * Scans process memory for loaded DEX files and dumps them to disk.
 * Returns count of DEX files dumped.
 */
extern int phantom_dex_dump(const char *output_dir);

static JSValue js_ph_dumpDex(JSContext *ctx, JSValueConst _this,
                               int argc, JSValueConst *argv) {
    (void)_this;
    const char *dir = "/data/phantom/dumps";
    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        dir = JS_ToCString(ctx, argv[0]);
        if (!dir) return JS_EXCEPTION;
    }
    int n = phantom_dex_dump(dir);
    if (argc > 0 && !JS_IsUndefined(argv[0])) JS_FreeCString(ctx, dir);
    return JS_NewInt32(ctx, n);
}

void register_api_ph(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    /* ph object */
    JSValue ph = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ph, "log",   JS_NewCFunction(ctx, js_ph_log,   "log",   1));
    JS_SetPropertyStr(ctx, ph, "warn",  JS_NewCFunction(ctx, js_ph_warn,  "warn",  1));
    JS_SetPropertyStr(ctx, ph, "error", JS_NewCFunction(ctx, js_ph_error, "error", 1));
    JS_SetPropertyStr(ctx, ph, "sleep", JS_NewCFunction(ctx, js_ph_sleep, "sleep", 1));
    JS_SetPropertyStr(ctx, ph, "env",    JS_NewCFunction(ctx, js_ph_env,    "env",    0));
    JS_SetPropertyStr(ctx, ph, "dumpDex",JS_NewCFunction(ctx, js_ph_dumpDex,"dumpDex",0));

    JS_SetPropertyStr(ctx, global, "ph", ph);

    /* also wire console.log → ph.log for compatibility */
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",   JS_NewCFunction(ctx, js_ph_log,   "log",   1));
    JS_SetPropertyStr(ctx, console, "warn",  JS_NewCFunction(ctx, js_ph_warn,  "warn",  1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, js_ph_error, "error", 1));
    JS_SetPropertyStr(ctx, global, "console", console);

    JS_FreeValue(ctx, global);
}

/*
 * phantom_exec_quickjs — entry point called from Rust.
 *
 * script_name: used as bytecode cache key. Pass the actual filename so each
 * script gets its own cache slot under /data/phantom/cache/.
 */
__attribute__((visibility("default")))
int phantom_exec_quickjs(const char *js, size_t len,
                          int pid, const char *script_name) {
    phantom_rt_t *prt = phantom_rt_create();
    if (!prt) return -1;

    phantom_rt_register_apis(prt, (pid_t)pid);

    const char *name = (script_name && *script_name) ? script_name : "anonymous.js";
    __android_log_print(ANDROID_LOG_DEBUG, TAG, "exec %s (%zu bytes)", name, len);

    int res = phantom_rt_exec(prt, js, len, name);
    if (res < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "script %s failed", name);
    }

    phantom_rt_destroy(prt);
    return res;
}
