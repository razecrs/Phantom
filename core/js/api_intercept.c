#include "quickjs/quickjs.h"
#include "../hook/shadowhook.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define TAG "Phantom/Intercept"

/*
 * Interceptor API for QuickJS scripts:
 *
 *   Interceptor.attach(addr, { onEnter(args){}, onLeave(retval){} })
 *   Interceptor.detachAll()
 *   Interceptor.replace(addr, replacement_addr)
 *
 * Implementation: fixed pool of 64 hook slots. Each slot gets its own
 * stub function in the STUBS array. When the target function is called,
 * the corresponding stub dispatches to the JS callbacks via dispatch_hook().
 *
 * Arguments are exposed as a JS array of BigInt (pointer-sized). Scripts
 * can read them with Memory.readU32/U64 etc. Return value is also a BigInt.
 *
 * Limitations:
 *   - max 64 concurrent hooks
 *   - argument count is fixed at 8 (register args a0-a7)
 *   - variadic functions may behave unexpectedly
 */

#define MAX_HOOKS 64

typedef struct {
    JSContext *ctx;
    JSValue    on_enter;
    JSValue    on_leave;
    void      *orig;      /* original function pointer (from shadowhook) */
    void      *stub;      /* shadowhook stub for unhooking */
    uintptr_t  addr;
    uint8_t    active;
} hook_slot_t;

static hook_slot_t g_slots[MAX_HOOKS];

/* ── generic dispatch ────────────────────────────────────────────────────
 *
 * Called by every stub with its slot index and the 8 GPR arguments.
 * Returns the return value from the original (or from onLeave).
 */
static uintptr_t dispatch_hook(int slot,
                                uintptr_t a0, uintptr_t a1,
                                uintptr_t a2, uintptr_t a3,
                                uintptr_t a4, uintptr_t a5,
                                uintptr_t a6, uintptr_t a7) {
    hook_slot_t *s = &g_slots[slot];
    if (!s->active) return 0;

    JSContext *ctx = s->ctx;
    uintptr_t retval = 0;

    /* build args array */
    JSValue args_arr = JS_NewArray(ctx);
    uintptr_t raw[8] = {a0, a1, a2, a3, a4, a5, a6, a7};
    for (int i = 0; i < 8; i++)
        JS_SetPropertyUint32(ctx, args_arr, (uint32_t)i,
                             JS_NewBigUint64(ctx, (uint64_t)raw[i]));

    /* onEnter(args) */
    if (!JS_IsUndefined(s->on_enter) && !JS_IsNull(s->on_enter)) {
        JSValue v = JS_Call(ctx, s->on_enter, JS_UNDEFINED, 1, &args_arr);
        JS_FreeValue(ctx, v);
    }

    /* call original via shadowhook's original pointer */
    typedef uintptr_t (*fn8_t)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,
                                uintptr_t,uintptr_t,uintptr_t,uintptr_t);
    if (s->orig) {
        retval = ((fn8_t)s->orig)(a0, a1, a2, a3, a4, a5, a6, a7);
    }

    /* onLeave(retval) — script can return a replacement value */
    if (!JS_IsUndefined(s->on_leave) && !JS_IsNull(s->on_leave)) {
        JSValue rv_js = JS_NewBigUint64(ctx, (uint64_t)retval);
        JSValue v = JS_Call(ctx, s->on_leave, JS_UNDEFINED, 1, &rv_js);
        JS_FreeValue(ctx, rv_js);
        /* if onLeave returns a BigInt, use it as new retval */
        if (JS_IsBigInt(ctx, v)) {
            uint64_t new_rv = 0;
            JS_ToIndex(ctx, &new_rv, v);
            retval = (uintptr_t)new_rv;
        }
        JS_FreeValue(ctx, v);
    }

    JS_FreeValue(ctx, args_arr);
    return retval;
}

/* ── stub pool — one stub function per slot ──────────────────────────────
 * Each stub captures its slot index by being at a known position.
 * We use a macro to generate 64 identical stubs that only differ by
 * the literal slot index they pass to dispatch_hook.
 */
#define MAKE_STUB(N)                                                            \
static uintptr_t hook_stub_##N(uintptr_t a0, uintptr_t a1,                    \
                                uintptr_t a2, uintptr_t a3,                    \
                                uintptr_t a4, uintptr_t a5,                    \
                                uintptr_t a6, uintptr_t a7) {                  \
    return dispatch_hook(N, a0,a1,a2,a3,a4,a5,a6,a7);                         \
}

MAKE_STUB(0)  MAKE_STUB(1)  MAKE_STUB(2)  MAKE_STUB(3)
MAKE_STUB(4)  MAKE_STUB(5)  MAKE_STUB(6)  MAKE_STUB(7)
MAKE_STUB(8)  MAKE_STUB(9)  MAKE_STUB(10) MAKE_STUB(11)
MAKE_STUB(12) MAKE_STUB(13) MAKE_STUB(14) MAKE_STUB(15)
MAKE_STUB(16) MAKE_STUB(17) MAKE_STUB(18) MAKE_STUB(19)
MAKE_STUB(20) MAKE_STUB(21) MAKE_STUB(22) MAKE_STUB(23)
MAKE_STUB(24) MAKE_STUB(25) MAKE_STUB(26) MAKE_STUB(27)
MAKE_STUB(28) MAKE_STUB(29) MAKE_STUB(30) MAKE_STUB(31)
MAKE_STUB(32) MAKE_STUB(33) MAKE_STUB(34) MAKE_STUB(35)
MAKE_STUB(36) MAKE_STUB(37) MAKE_STUB(38) MAKE_STUB(39)
MAKE_STUB(40) MAKE_STUB(41) MAKE_STUB(42) MAKE_STUB(43)
MAKE_STUB(44) MAKE_STUB(45) MAKE_STUB(46) MAKE_STUB(47)
MAKE_STUB(48) MAKE_STUB(49) MAKE_STUB(50) MAKE_STUB(51)
MAKE_STUB(52) MAKE_STUB(53) MAKE_STUB(54) MAKE_STUB(55)
MAKE_STUB(56) MAKE_STUB(57) MAKE_STUB(58) MAKE_STUB(59)
MAKE_STUB(60) MAKE_STUB(61) MAKE_STUB(62) MAKE_STUB(63)

typedef uintptr_t (*stub_fn_t)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,
                                uintptr_t,uintptr_t,uintptr_t,uintptr_t);

static const stub_fn_t STUBS[MAX_HOOKS] = {
    hook_stub_0,  hook_stub_1,  hook_stub_2,  hook_stub_3,
    hook_stub_4,  hook_stub_5,  hook_stub_6,  hook_stub_7,
    hook_stub_8,  hook_stub_9,  hook_stub_10, hook_stub_11,
    hook_stub_12, hook_stub_13, hook_stub_14, hook_stub_15,
    hook_stub_16, hook_stub_17, hook_stub_18, hook_stub_19,
    hook_stub_20, hook_stub_21, hook_stub_22, hook_stub_23,
    hook_stub_24, hook_stub_25, hook_stub_26, hook_stub_27,
    hook_stub_28, hook_stub_29, hook_stub_30, hook_stub_31,
    hook_stub_32, hook_stub_33, hook_stub_34, hook_stub_35,
    hook_stub_36, hook_stub_37, hook_stub_38, hook_stub_39,
    hook_stub_40, hook_stub_41, hook_stub_42, hook_stub_43,
    hook_stub_44, hook_stub_45, hook_stub_46, hook_stub_47,
    hook_stub_48, hook_stub_49, hook_stub_50, hook_stub_51,
    hook_stub_52, hook_stub_53, hook_stub_54, hook_stub_55,
    hook_stub_56, hook_stub_57, hook_stub_58, hook_stub_59,
    hook_stub_60, hook_stub_61, hook_stub_62, hook_stub_63,
};

/* ── Interceptor.attach(addr, {onEnter, onLeave}) → id ───────────────── */
static JSValue js_interceptor_attach(JSContext *ctx, JSValueConst _this,
                                      int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;

    /* find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_HOOKS; i++) {
        if (!g_slots[i].active) { slot = i; break; }
    }
    if (slot < 0)
        return JS_ThrowRangeError(ctx, "Interceptor: max %d hooks reached", MAX_HOOKS);

    hook_slot_t *s = &g_slots[slot];
    s->ctx      = ctx;
    s->addr     = (uintptr_t)addr;
    s->on_enter = JS_GetPropertyStr(ctx, argv[1], "onEnter");
    s->on_leave = JS_GetPropertyStr(ctx, argv[1], "onLeave");
    s->stub     = shadowhook_hook_func_addr(
                      (void *)(uintptr_t)addr,
                      (void *)STUBS[slot],
                      &s->orig);
    if (!s->stub) {
        JS_FreeValue(ctx, s->on_enter);
        JS_FreeValue(ctx, s->on_leave);
        __android_log_print(ANDROID_LOG_WARN, TAG,
            "attach failed for addr %lx", (unsigned long)addr);
        return JS_ThrowInternalError(ctx, "shadowhook_hook_func_addr failed");
    }
    s->active = 1;
    __android_log_print(ANDROID_LOG_INFO, TAG,
        "hooked slot %d addr %lx", slot, (unsigned long)addr);
    return JS_NewInt32(ctx, slot); /* return slot id for detach */
}

/* ── Interceptor.detach(id) ─────────────────────────────────────────────*/
static JSValue js_interceptor_detach(JSContext *ctx, JSValueConst _this,
                                      int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    int32_t slot = 0;
    JS_ToInt32(ctx, &slot, argv[0]);
    if (slot < 0 || slot >= MAX_HOOKS) return JS_UNDEFINED;
    hook_slot_t *s = &g_slots[slot];
    if (!s->active) return JS_UNDEFINED;
    if (s->stub) shadowhook_unhook(s->stub);
    JS_FreeValue(ctx, s->on_enter);
    JS_FreeValue(ctx, s->on_leave);
    memset(s, 0, sizeof(*s));
    return JS_UNDEFINED;
}

/* ── Interceptor.detachAll() ─────────────────────────────────────────── */
static JSValue js_interceptor_detach_all(JSContext *ctx, JSValueConst _this,
                                          int argc, JSValueConst *argv) {
    (void)_this; (void)argc; (void)argv;
    for (int i = 0; i < MAX_HOOKS; i++) {
        if (!g_slots[i].active) continue;
        if (g_slots[i].stub) shadowhook_unhook(g_slots[i].stub);
        JS_FreeValue(ctx, g_slots[i].on_enter);
        JS_FreeValue(ctx, g_slots[i].on_leave);
        memset(&g_slots[i], 0, sizeof(g_slots[i]));
    }
    return JS_UNDEFINED;
}

/* ── Interceptor.replace(addr, replacement) ──────────────────────────── */
static JSValue js_interceptor_replace(JSContext *ctx, JSValueConst _this,
                                       int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0, repl = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &repl, argv[1])) return JS_EXCEPTION;
    void *orig = NULL;
    void *stub = shadowhook_hook_func_addr(
        (void *)(uintptr_t)addr,
        (void *)(uintptr_t)repl,
        &orig);
    if (!stub) return JS_ThrowInternalError(ctx, "replace failed");
    /* return original pointer so script can call-through if desired */
    return JS_NewBigUint64(ctx, (uint64_t)(uintptr_t)orig);
}

void register_api_intercept(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue obj = JS_NewObject(ctx);

#define B(n, fn, argc) JS_SetPropertyStr(ctx, obj, n, JS_NewCFunction(ctx, fn, n, argc))
    B("attach",     js_interceptor_attach,     2);
    B("detach",     js_interceptor_detach,     1);
    B("detachAll",  js_interceptor_detach_all, 0);
    B("replace",    js_interceptor_replace,    2);
#undef B

    JS_SetPropertyStr(ctx, global, "Interceptor", obj);
    JS_FreeValue(ctx, global);
}
