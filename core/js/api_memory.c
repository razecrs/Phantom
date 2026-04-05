#include "quickjs/quickjs.h"
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <android/log.h>

#define TAG "Phantom/Memory"

extern int neon_scan_u32(const void *mem, size_t len, uint32_t val,
                          size_t *out_offsets, int max_results);

/* ── Memory.read* ─────────────────────────────────────────────────────── */

#define MAKE_READ(name, type, jsconv)                                           \
static JSValue js_mem_##name(JSContext *ctx, JSValueConst _this,                \
                              int argc, JSValueConst *argv) {                   \
    (void)_this; (void)argc;                                                    \
    uint64_t addr = 0;                                                          \
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;                  \
    type val = *(const type *)(uintptr_t)addr;                                  \
    return jsconv;                                                              \
}

MAKE_READ(readU8,     uint8_t,  JS_NewUint32(ctx, (uint32_t)val))
MAKE_READ(readS8,     int8_t,   JS_NewInt32(ctx,  (int32_t)val))
MAKE_READ(readU16,    uint16_t, JS_NewUint32(ctx, (uint32_t)val))
MAKE_READ(readS16,    int16_t,  JS_NewInt32(ctx,  (int32_t)val))
MAKE_READ(readU32,    uint32_t, JS_NewUint32(ctx, val))
MAKE_READ(readS32,    int32_t,  JS_NewInt32(ctx,  val))
MAKE_READ(readU64,    uint64_t, JS_NewBigUint64(ctx, val))
MAKE_READ(readS64,    int64_t,  JS_NewBigInt64(ctx,  val))
MAKE_READ(readFloat,  float,    JS_NewFloat64(ctx, (double)val))
MAKE_READ(readDouble, double,   JS_NewFloat64(ctx, val))

static JSValue js_mem_readByteArray(JSContext *ctx, JSValueConst _this,
                                     int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0; uint32_t len = 0;
    if (JS_ToIndex(ctx,  &addr, argv[0])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &len,  argv[1])) return JS_EXCEPTION;
    if (len > 1024 * 1024) return JS_ThrowRangeError(ctx, "len > 1 MB");
    return JS_NewArrayBufferCopy(ctx, (const uint8_t *)(uintptr_t)addr, len);
}

static JSValue js_mem_readCString(JSContext *ctx, JSValueConst _this,
                                   int argc, JSValueConst *argv) {
    (void)_this;
    uint64_t addr = 0; uint32_t maxlen = 256;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    if (argc > 1) JS_ToUint32(ctx, &maxlen, argv[1]);
    if (maxlen > 65536) maxlen = 65536;
    const char *s = (const char *)(uintptr_t)addr;
    return JS_NewStringLen(ctx, s, strnlen(s, maxlen));
}

static JSValue js_mem_readPointer(JSContext *ctx, JSValueConst _this,
                                   int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    return JS_NewBigUint64(ctx, (uint64_t)*(uintptr_t *)(uintptr_t)addr);
}

/* ── Memory.write* ────────────────────────────────────────────────────── */

#define MAKE_WRITE_UINT(name, type)                                             \
static JSValue js_mem_##name(JSContext *ctx, JSValueConst _this,                \
                              int argc, JSValueConst *argv) {                   \
    (void)_this; (void)argc;                                                    \
    uint64_t addr = 0; uint32_t val = 0;                                        \
    if (JS_ToIndex(ctx,  &addr, argv[0])) return JS_EXCEPTION;                 \
    if (JS_ToUint32(ctx, &val,  argv[1])) return JS_EXCEPTION;                 \
    *(type *)(uintptr_t)addr = (type)val;                                       \
    return JS_UNDEFINED;                                                        \
}

#define MAKE_WRITE_INT(name, type)                                              \
static JSValue js_mem_##name(JSContext *ctx, JSValueConst _this,                \
                              int argc, JSValueConst *argv) {                   \
    (void)_this; (void)argc;                                                    \
    uint64_t addr = 0; int32_t val = 0;                                         \
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;                  \
    if (JS_ToInt32(ctx, &val,  argv[1])) return JS_EXCEPTION;                  \
    *(type *)(uintptr_t)addr = (type)val;                                       \
    return JS_UNDEFINED;                                                        \
}

MAKE_WRITE_UINT(writeU8,  uint8_t)
MAKE_WRITE_INT (writeS8,  int8_t)
MAKE_WRITE_UINT(writeU16, uint16_t)
MAKE_WRITE_INT (writeS16, int16_t)
MAKE_WRITE_UINT(writeU32, uint32_t)
MAKE_WRITE_INT (writeS32, int32_t)

static JSValue js_mem_writeU64(JSContext *ctx, JSValueConst _this,
                                int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0, val = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &val,  argv[1])) return JS_EXCEPTION;
    *(uint64_t *)(uintptr_t)addr = val;
    return JS_UNDEFINED;
}

static JSValue js_mem_writeFloat(JSContext *ctx, JSValueConst _this,
                                  int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0; double d = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    JS_ToFloat64(ctx, &d, argv[1]);
    *(float *)(uintptr_t)addr = (float)d;
    return JS_UNDEFINED;
}

static JSValue js_mem_writeDouble(JSContext *ctx, JSValueConst _this,
                                   int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0; double val = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    JS_ToFloat64(ctx, &val, argv[1]);
    *(double *)(uintptr_t)addr = val;
    return JS_UNDEFINED;
}

static JSValue js_mem_writeByteArray(JSContext *ctx, JSValueConst _this,
                                      int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    size_t blen = 0;
    uint8_t *buf = JS_GetArrayBuffer(ctx, &blen, argv[1]);
    if (!buf) return JS_ThrowTypeError(ctx, "arg 1 must be ArrayBuffer");
    memcpy((void *)(uintptr_t)addr, buf, blen);
    return JS_UNDEFINED;
}

static JSValue js_mem_writePointer(JSContext *ctx, JSValueConst _this,
                                    int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0, ptr = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &ptr,  argv[1])) return JS_EXCEPTION;
    *(uintptr_t *)(uintptr_t)addr = (uintptr_t)ptr;
    return JS_UNDEFINED;
}

static JSValue js_mem_alloc(JSContext *ctx, JSValueConst _this,
                              int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint32_t size = 0;
    if (JS_ToUint32(ctx, &size, argv[0])) return JS_EXCEPTION;
    void *p = malloc(size);
    if (!p) return JS_ThrowRangeError(ctx, "malloc failed");
    return JS_NewBigUint64(ctx, (uint64_t)(uintptr_t)p);
}

static JSValue js_mem_free(JSContext *ctx, JSValueConst _this,
                             int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0;
    if (JS_ToIndex(ctx, &addr, argv[0])) return JS_EXCEPTION;
    free((void *)(uintptr_t)addr);
    return JS_UNDEFINED;
}

static JSValue js_mem_protect(JSContext *ctx, JSValueConst _this,
                               int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0; uint32_t size = 0;
    if (JS_ToIndex(ctx,  &addr, argv[0])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &size, argv[1])) return JS_EXCEPTION;
    const char *ps = JS_ToCString(ctx, argv[2]);
    int prot = 0;
    if (ps) {
        if (strchr(ps, 'r')) prot |= PROT_READ;
        if (strchr(ps, 'w')) prot |= PROT_WRITE;
        if (strchr(ps, 'x')) prot |= PROT_EXEC;
        JS_FreeCString(ctx, ps);
    }
    long page = sysconf(_SC_PAGE_SIZE);
    uintptr_t base = (uintptr_t)addr & ~(uintptr_t)(page - 1);
    mprotect((void *)base, (size_t)(size + (addr - base)), prot);
    return JS_UNDEFINED;
}

static JSValue js_mem_scan(JSContext *ctx, JSValueConst _this,
                             int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    uint64_t addr = 0, len = 0; uint32_t val = 0;
    if (JS_ToIndex(ctx,  &addr, argv[0])) return JS_EXCEPTION;
    if (JS_ToIndex(ctx,  &len,  argv[1])) return JS_EXCEPTION;
    if (JS_ToUint32(ctx, &val,  argv[2])) return JS_EXCEPTION;
    size_t offsets[4096];
    int n = neon_scan_u32((void *)(uintptr_t)addr, (size_t)len, val, offsets, 4096);
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < n; i++)
        JS_SetPropertyUint32(ctx, arr, (uint32_t)i,
                             JS_NewBigUint64(ctx, addr + (uint64_t)offsets[i]));
    return arr;
}

/* ── Module API ─────────────────────────────────────────────────────────
 * Module.findBaseAddress(name)        → BigInt | null
 * Module.findExportByName(so, sym)    → BigInt | null
 * Module.getByName(name)              → {base, size} | null
 * Module.enumerateExports(name)       → Array<{name, address}>
 */
static uintptr_t maps_find_base(const char *name, uintptr_t *out_end) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0, end = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, name)) continue;
        uintptr_t s = 0, e = 0;
        sscanf(line, "%lx-%lx", &s, &e);
        if (!base) base = s;
        end = e;
    }
    fclose(f);
    if (out_end) *out_end = end;
    return base;
}

static JSValue js_mod_findBase(JSContext *ctx, JSValueConst _this,
                                int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    uintptr_t base = maps_find_base(name, NULL);
    JS_FreeCString(ctx, name);
    return base ? JS_NewBigUint64(ctx, (uint64_t)base) : JS_NULL;
}

static JSValue js_mod_findExport(JSContext *ctx, JSValueConst _this,
                                  int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    const char *so  = JS_ToCString(ctx, argv[0]);
    const char *sym = JS_ToCString(ctx, argv[1]);
    if (!so || !sym) { JS_FreeCString(ctx, so); JS_FreeCString(ctx, sym); return JS_EXCEPTION; }
    void *handle = dlopen(so, RTLD_NOLOAD | RTLD_NOW);
    JSValue result = JS_NULL;
    if (handle) {
        void *addr = dlsym(handle, sym);
        if (addr) result = JS_NewBigUint64(ctx, (uint64_t)(uintptr_t)addr);
        dlclose(handle);
    }
    JS_FreeCString(ctx, so);
    JS_FreeCString(ctx, sym);
    return result;
}

static JSValue js_mod_getByName(JSContext *ctx, JSValueConst _this,
                                 int argc, JSValueConst *argv) {
    (void)_this; (void)argc;
    const char *name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    uintptr_t end = 0, base = maps_find_base(name, &end);
    JS_FreeCString(ctx, name);
    if (!base) return JS_NULL;
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "base", JS_NewBigUint64(ctx, (uint64_t)base));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewUint32(ctx, (uint32_t)(end - base)));
    return obj;
}

/* ── Process API ────────────────────────────────────────────────────────
 * Process.id, .arch, .pageSize, .enumerateModules()
 */
static JSValue js_proc_enumModules(JSContext *ctx, JSValueConst _this,
                                    int argc, JSValueConst *argv) {
    (void)_this; (void)argc; (void)argv;
    JSValue arr = JS_NewArray(ctx);
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return arr;
    char line[512], last[256] = {0};
    uint32_t idx = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, " r-xp ")) continue;
        char *p = strrchr(line, '/');
        if (!p) continue;
        char so[128] = {0};
        sscanf(p + 1, "%127[^\n]", so);
        if (!so[0] || strcmp(so, last) == 0) continue;
        strncpy(last, so, sizeof(last) - 1);
        uintptr_t base = 0;
        sscanf(line, "%lx-", &base);
        JSValue mod = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, mod, "name", JS_NewString(ctx, so));
        JS_SetPropertyStr(ctx, mod, "base", JS_NewBigUint64(ctx, (uint64_t)base));
        JS_SetPropertyUint32(ctx, arr, idx++, mod);
    }
    fclose(f);
    return arr;
}

void register_api_memory(JSContext *ctx, pid_t pid) {
    (void)pid;
    JSValue global = JS_GetGlobalObject(ctx);

#define B(obj, jsname, fn, n) \
    JS_SetPropertyStr(ctx, obj, jsname, JS_NewCFunction(ctx, fn, jsname, n))

    JSValue mem = JS_NewObject(ctx);
    B(mem, "readU8",        js_mem_readU8,        1);
    B(mem, "readS8",        js_mem_readS8,        1);
    B(mem, "readU16",       js_mem_readU16,       1);
    B(mem, "readS16",       js_mem_readS16,       1);
    B(mem, "readU32",       js_mem_readU32,       1);
    B(mem, "readS32",       js_mem_readS32,       1);
    B(mem, "readU64",       js_mem_readU64,       1);
    B(mem, "readS64",       js_mem_readS64,       1);
    B(mem, "readFloat",     js_mem_readFloat,     1);
    B(mem, "readDouble",    js_mem_readDouble,    1);
    B(mem, "readByteArray", js_mem_readByteArray, 2);
    B(mem, "readCString",   js_mem_readCString,   1);
    B(mem, "readPointer",   js_mem_readPointer,   1);
    B(mem, "writeU8",       js_mem_writeU8,       2);
    B(mem, "writeS8",       js_mem_writeS8,       2);
    B(mem, "writeU16",      js_mem_writeU16,      2);
    B(mem, "writeS16",      js_mem_writeS16,      2);
    B(mem, "writeU32",      js_mem_writeU32,      2);
    B(mem, "writeS32",      js_mem_writeS32,      2);
    B(mem, "writeU64",      js_mem_writeU64,      2);
    B(mem, "writeFloat",    js_mem_writeFloat,    2);
    B(mem, "writeDouble",   js_mem_writeDouble,   2);
    B(mem, "writeByteArray",js_mem_writeByteArray,2);
    B(mem, "writePointer",  js_mem_writePointer,  2);
    B(mem, "alloc",         js_mem_alloc,         1);
    B(mem, "free",          js_mem_free,          1);
    B(mem, "protect",       js_mem_protect,       3);
    B(mem, "scan",          js_mem_scan,          3);
    JS_SetPropertyStr(ctx, global, "Memory", mem);

    JSValue mod = JS_NewObject(ctx);
    B(mod, "findBaseAddress",  js_mod_findBase,   1);
    B(mod, "findExportByName", js_mod_findExport, 2);
    B(mod, "getByName",        js_mod_getByName,  1);
    JS_SetPropertyStr(ctx, global, "Module", mod);

    JSValue proc = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proc, "id",       JS_NewInt32(ctx, (int32_t)getpid()));
    JS_SetPropertyStr(ctx, proc, "pageSize", JS_NewInt32(ctx, (int32_t)sysconf(_SC_PAGE_SIZE)));
#if defined(__aarch64__)
    JS_SetPropertyStr(ctx, proc, "arch", JS_NewString(ctx, "arm64"));
#else
    JS_SetPropertyStr(ctx, proc, "arch", JS_NewString(ctx, "arm"));
#endif
    B(proc, "enumerateModules", js_proc_enumModules, 0);
    JS_SetPropertyStr(ctx, global, "Process", proc);

#undef B
    JS_FreeValue(ctx, global);
}
