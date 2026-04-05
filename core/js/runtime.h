#pragma once
#include "quickjs/quickjs.h"
#include <stdint.h>

typedef struct phantom_rt phantom_rt_t;

phantom_rt_t *phantom_rt_create(void);
void          phantom_rt_destroy(phantom_rt_t *rt);

/* load and execute a JS script (bytecode cached on first run) */
int phantom_rt_exec(phantom_rt_t *rt, const char *js, size_t len,
                    const char *filename);

/* load precompiled bytecode directly — zero parse overhead */
int phantom_rt_exec_bc(phantom_rt_t *rt, const uint8_t *bc, size_t len);

/* compile JS → bytecode, write to cache path */
int phantom_rt_compile(phantom_rt_t *rt, const char *js, size_t len,
                       const char *cache_path);

/* register all phantom APIs: Java, Interceptor, Memory, ph */
void          phantom_rt_register_apis(phantom_rt_t *rt, pid_t target_pid);
JSContext    *phantom_rt_get_ctx(phantom_rt_t *rt);
