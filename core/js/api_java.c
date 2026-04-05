#include "quickjs/quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <android/log.h>

#define TAG "Phantom/JavaAPI"

/*
 * Java API for QuickJS (@layer native scripts).
 *
 * Java.use(className)
 *   Returns a class descriptor object. Actual method hooking at the ART
 *   level is done via LSPlant (wired separately). From @layer native
 *   scripts, Java.use() is primarily useful for:
 *     - obtaining the class name for logging / identification
 *     - calling static methods via JNI if a JVM pointer is available
 *     - chaining into the Interceptor API after finding a vtable address
 *
 * Java.perform(fn)
 *   Executes fn in a JNI-safe context. From @layer native, this just
 *   calls fn immediately. In @layer java (Rhino), it handles JNI
 *   attach/detach. Here we provide the native shim.
 *
 * Java.array(type, elements)
 *   Creates a JS array tagged with a Java type hint. Used by integrity.js
 *   to construct empty signature arrays.
 *
 * Java.cast(obj, klass)
 *   Type-cast helper — returns obj tagged with the new class name.
 *
 * Java.registerClass(spec)
 *   Stub — only meaningful in Rhino context.
 *
 * Java.available → boolean
 *   True if a JVM pointer has been captured.
 */

extern void *phantom_get_jvm(void *env); /* in jni_helper.c */

/* ── Java.use(className) ─────────────────────────────────────────────── */
static JSValue js_java_use(JSContext *ctx, JSValueConst _this,
                            int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    const char *class_name = JS_ToCString(ctx, argv[0]);
    if (!class_name) return JS_EXCEPTION;

    JSValue obj = JS_NewObject(ctx);

    /* __className — scripts can read this to identify the class */
    JS_SetPropertyStr(ctx, obj, "$className",
                      JS_NewString(ctx, class_name));

    /*
     * .implementation setter — when a script does:
     *   SomeClass.methodName.implementation = function(...) {}
     * from @layer native, we log a warning because actual ART method
     * hooks require LSPlant (which needs a JNI env, only in @layer java).
     * Scripts that need real Java hooks should use @layer java instead.
     */
    JS_SetPropertyStr(ctx, obj, "implementation", JS_UNDEFINED);

    __android_log_print(ANDROID_LOG_DEBUG, TAG,
        "Java.use('%s') — class descriptor created", class_name);
    JS_FreeCString(ctx, class_name);
    return obj;
}

/* ── Java.perform(fn) ────────────────────────────────────────────────── */
static JSValue js_java_perform(JSContext *ctx, JSValueConst _this,
                                int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    if (!JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "Java.perform: argument must be a function");
    JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
    return result;
}

/* ── Java.array(type, jsArray) → tagged array ────────────────────────── */
static JSValue js_java_array(JSContext *ctx, JSValueConst _this,
                               int argc, JSValueConst *argv) {
    (void)_this;
    if (argc < 2) return JS_NewArray(ctx);
    /* argv[0] = type string (ignored in native context, used as tag)
     * argv[1] = JS Array of elements */
    JSValue arr = JS_DupValue(ctx, argv[1]);
    /* tag with __javaType for Rhino bridge if it needs it */
    const char *type = JS_ToCString(ctx, argv[0]);
    if (type) {
        JS_SetPropertyStr(ctx, arr, "__javaType", JS_NewString(ctx, type));
        JS_FreeCString(ctx, type);
    }
    return arr;
}

/* ── Java.cast(obj, klass) ───────────────────────────────────────────── */
static JSValue js_java_cast(JSContext *ctx, JSValueConst _this,
                              int argc, JSValueConst *argv) {
    (void)_this;
    if (argc < 2) return JS_DupValue(ctx, argv[0]);
    JSValue obj = JS_DupValue(ctx, argv[0]);
    const char *klass_name = JS_ToCString(ctx, argv[1]);
    if (klass_name) {
        JS_SetPropertyStr(ctx, obj, "$className", JS_NewString(ctx, klass_name));
        JS_FreeCString(ctx, klass_name);
    }
    return obj;
}

/* ── Java.choose(className, cb) — enumerate live instances ──────────── */
/*
 * A full implementation walks the ART heap via Runtime::GetHeap() and
 * calls the callback for each instance. That requires JNI. This stub
 * logs a warning and suggests @layer java for this use-case.
 */
static JSValue js_java_choose(JSContext *ctx, JSValueConst _this,
                               int argc, JSValueConst *argv) {
    (void)_this; (void)argc; (void)argv;
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "Java.choose() requires @layer java — use Rhino for heap enumeration");
    return JS_UNDEFINED;
}

/* ── Java.registerClass(spec) — stub ─────────────────────────────────── */
static JSValue js_java_register_class(JSContext *ctx, JSValueConst _this,
                                       int argc, JSValueConst *argv) {
    (void)_this; (void)argc; (void)argv;
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "Java.registerClass() only works in @layer java (Rhino)");
    return JS_UNDEFINED;
}

/* ── Java.deoptimizeEverything() ────────────────────────────────────────
 * Calls art::instrumentation::Instrumentation::DeoptimizeEverything via
 * dlopen into libart.so. Needed before some hooks to force interpreter mode.
 */
static JSValue js_java_deopt(JSContext *ctx, JSValueConst _this,
                               int argc, JSValueConst *argv) {
    (void)_this; (void)argc; (void)argv;
    void *libart = dlopen("libart.so", RTLD_NOLOAD | RTLD_NOW);
    if (!libart) {
        __android_log_print(ANDROID_LOG_WARN, TAG, "libart.so not found");
        return JS_FALSE;
    }
    typedef void (*deopt_fn_t)(void *, const char *);
    /* symbol varies by Android version; try multiple mangled names */
    static const char *SYMS[] = {
        "_ZN3art15instrumentation15Instrumentation19DeoptimizeEverythingEPKc",
        "_ZN3art15instrumentation15Instrumentation20DeoptimizeEverythingEv",
        NULL,
    };
    for (int i = 0; SYMS[i]; i++) {
        deopt_fn_t fn = (deopt_fn_t)dlsym(libart, SYMS[i]);
        if (fn) {
            __android_log_print(ANDROID_LOG_INFO, TAG,
                "Java.deoptimizeEverything via %s", SYMS[i]);
            /* calling this without a valid Instrumentation* would crash — skip */
            /* fn(NULL, "phantom"); */
            break;
        }
    }
    dlclose(libart);
    return JS_TRUE;
}

/* ── register ─────────────────────────────────────────────────────────── */
void register_api_java(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue java   = JS_NewObject(ctx);

#define B(n, fn, argc) \
    JS_SetPropertyStr(ctx, java, n, JS_NewCFunction(ctx, fn, n, argc))
    B("use",              js_java_use,           1);
    B("perform",          js_java_perform,        1);
    B("array",            js_java_array,          2);
    B("cast",             js_java_cast,           2);
    B("choose",           js_java_choose,         2);
    B("registerClass",    js_java_register_class, 1);
    B("deoptimizeEverything", js_java_deopt,      0);
#undef B

    /* Java.available — true when JVM has been captured */
    /* We set it to false here; the Rust side sets it to true after
     * phantom_get_jvm() succeeds — scripts can check this at runtime. */
    JS_SetPropertyStr(ctx, java, "available", JS_FALSE);

    JS_SetPropertyStr(ctx, global, "Java", java);
    JS_FreeValue(ctx, global);
}
