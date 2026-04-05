#include "shadowhook.h"
#include <android/log.h>

#define TAG "phantom/shadowhook"

int shadowhook_init(int mode, int debuggable) {
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "shadowhook_init: stub — real ShadowHook SO not loaded");
    (void)mode; (void)debuggable;
    return -1;
}

void *shadowhook_hook_func_addr(void *func_addr, void *hook_func, void **orig) {
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "shadowhook_hook_func_addr: stub called");
    (void)func_addr; (void)hook_func; (void)orig;
    return NULL;
}

void *shadowhook_hook_sym_name(const char *lib_name, const char *sym_name,
                               void *hook_func, void **orig) {
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "shadowhook_hook_sym_name: stub — %s!%s", lib_name, sym_name);
    (void)hook_func; (void)orig;
    return NULL;
}

int shadowhook_unhook(void *stub) {
    (void)stub;
    return 0;
}

void *shadowhook_get_return_address(void) { return NULL; }
int   shadowhook_get_errno(void)          { return 0; }
const char *shadowhook_to_errmsg(int e)   { (void)e; return "stub"; }
