#include "runtime.h"
#include "quickjs/quickjs.h"
#include "../ipc/ringbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdint.h>

#define CACHE_DIR "/data/phantom/cache"

struct phantom_rt {
    JSRuntime *rt;
    JSContext *ctx;
    pid_t      target_pid;
};

phantom_rt_t *phantom_rt_create(void) {
    phantom_rt_t *prt = calloc(1, sizeof(*prt));
    prt->rt  = JS_NewRuntime();
    prt->ctx = JS_NewContext(prt->rt);
    JS_SetMemoryLimit(prt->rt, 32 * 1024 * 1024); /* 32MB cap */
    JS_SetMaxStackSize(prt->rt, 512 * 1024);
    return prt;
}

void phantom_rt_destroy(phantom_rt_t *prt) {
    JS_FreeContext(prt->ctx);
    JS_FreeRuntime(prt->rt);
    free(prt);
}

JSContext *phantom_rt_get_ctx(phantom_rt_t *prt) {
    return prt->ctx;
}

void register_api_java(JSContext *ctx);
void register_api_intercept(JSContext *ctx);
void register_api_memory(JSContext *ctx, pid_t pid);
void register_api_ph(JSContext *ctx);
void register_api_network(JSContext *ctx);

__attribute__((visibility("default")))
void phantom_rt_register_apis(phantom_rt_t *prt, pid_t pid) {
    prt->target_pid = pid;
    register_api_java(prt->ctx);
    register_api_intercept(prt->ctx);
    register_api_memory(prt->ctx, pid);
    register_api_ph(prt->ctx);
    register_api_network(prt->ctx);
}

/* ── bytecode cache helpers ───────────────────────────────────────────────
 *
 * Cache path: /data/phantom/cache/<basename>.qbc
 * We use a simple FNV-1a hash of the filename string as the cache key.
 * Invalidation: compare cached file size header against current source len.
 * If source len matches the uint32 stored at bytes 0-3 of the cache file,
 * we trust the cache. Cheap, dependency-free, good enough for scripts that
 * rarely change.
 */
static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

static void cache_path_for(const char *filename, char *out, size_t out_max) {
    uint32_t h = fnv1a(filename);
    snprintf(out, out_max, "%s/%08x.qbc", CACHE_DIR, h);
}

/* Try to load and execute bytecode from cache. Returns 0 on success. */
static int try_exec_cache(phantom_rt_t *prt, const char *cache, size_t src_len) {
    FILE *f = fopen(cache, "rb");
    if (!f) return -1;

    /* first 4 bytes = source length tag */
    uint32_t tag = 0;
    if (fread(&tag, 4, 1, f) != 1 || tag != (uint32_t)src_len) {
        fclose(f);
        return -1; /* stale cache */
    }

    fseek(f, 0, SEEK_END);
    long total = ftell(f);
    if (total <= 4) { fclose(f); return -1; }
    size_t bc_len = (size_t)(total - 4);
    uint8_t *bc = malloc(bc_len);
    if (!bc) { fclose(f); return -1; }

    fseek(f, 4, SEEK_SET);
    if (fread(bc, 1, bc_len, f) != bc_len) {
        fclose(f); free(bc); return -1;
    }
    fclose(f);

    int ret = phantom_rt_exec_bc(prt, bc, bc_len);
    free(bc);
    return ret;
}

/* Compile source, write cache file, then execute. */
static int compile_save_exec(phantom_rt_t *prt,
                              const char *js, size_t len,
                              const char *filename, const char *cache) {
    JSValue fn = JS_Eval(prt->ctx, js, len, filename,
                         JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn)) return -1;

    size_t bc_len = 0;
    uint8_t *bc = JS_WriteObject(prt->ctx, &bc_len, fn, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(prt->ctx, fn);
    if (!bc) return -1;

    /* ensure cache dir exists */
    mkdir(CACHE_DIR, 0755);

    FILE *f = fopen(cache, "wb");
    if (f) {
        uint32_t tag = (uint32_t)len;
        fwrite(&tag, 4, 1, f);        /* invalidation tag */
        fwrite(bc, 1, bc_len, f);
        fclose(f);
    }

    /* execute the already-compiled function object */
    JSValue v = JS_ReadObject(prt->ctx, bc, bc_len, JS_READ_OBJ_BYTECODE);
    js_free(prt->ctx, bc);
    if (JS_IsException(v)) return -1;

    JSValue result = JS_EvalFunction(prt->ctx, v);
    int err = JS_IsException(result);
    JS_FreeValue(prt->ctx, result);
    return err ? -1 : 0;
}

int phantom_rt_exec(phantom_rt_t *prt, const char *js, size_t len,
                    const char *filename) {
    /* try bytecode cache first */
    char cache[256];
    cache_path_for(filename, cache, sizeof(cache));

    if (try_exec_cache(prt, cache, len) == 0) {
        return 0;
    }

    /* cache miss — compile, save, execute */
    int ret = compile_save_exec(prt, js, len, filename, cache);
    if (ret == 0) return 0;

    /* compile failed — fall back to plain eval for error reporting */
    JSValue v = JS_Eval(prt->ctx, js, len, filename, JS_EVAL_TYPE_GLOBAL);
    int err = JS_IsException(v);
    if (err) {
        JSValue exc = JS_GetException(prt->ctx);
        const char *msg = JS_ToCString(prt->ctx, exc);
        if (msg) JS_FreeCString(prt->ctx, msg);
        JS_FreeValue(prt->ctx, exc);
    }
    JS_FreeValue(prt->ctx, v);
    return err ? -1 : 0;
}

int phantom_rt_compile(phantom_rt_t *prt, const char *js, size_t len,
                       const char *cache_path) {
    JSValue fn = JS_Eval(prt->ctx, js, len, cache_path,
                         JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(fn)) return -1;
    size_t bc_len;
    uint8_t *bc = JS_WriteObject(prt->ctx, &bc_len, fn,
                                 JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(prt->ctx, fn);
    if (!bc) return -1;
    FILE *f = fopen(cache_path, "wb");
    if (f) { fwrite(bc, 1, bc_len, f); fclose(f); }
    js_free(prt->ctx, bc);
    return 0;
}

int phantom_rt_exec_bc(phantom_rt_t *prt, const uint8_t *bc, size_t len) {
    JSValue fn = JS_ReadObject(prt->ctx, bc, len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(fn)) return -1;
    JSValue v = JS_EvalFunction(prt->ctx, fn);
    int err = JS_IsException(v);
    JS_FreeValue(prt->ctx, v);
    return err ? -1 : 0;
}
