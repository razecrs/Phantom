#pragma once
/*
 * shadowhook.h — ShadowHook C API declarations
 *
 * I use ShadowHook for native inline hooks — patching function entry points
 * in .so files loaded by the target app. The actual libshadowhook.so comes
 * from bytedance's pre-built AAR. These are just the C declarations.
 *
 * Two hook styles:
 *   - func_addr: hook by absolute address (I already know where it is)
 *   - sym_name:  hook by symbol name inside a specific .so
 *
 * Both return a stub pointer I keep to unhook later.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* shadowhook_init — call once at module load. mode: 0=unique, 1=shared */
int shadowhook_init(int mode, int debuggable);

/*
 * shadowhook_hook_func_addr — hook a function at a known address.
 *
 *   func_addr:  address of function to hook
 *   hook_func:  my replacement function (same signature)
 *   orig:       receives a pointer I call to invoke the original
 *
 * Returns opaque stub pointer, NULL on failure.
 */
void *shadowhook_hook_func_addr(void *func_addr,
                                void *hook_func,
                                void **orig);

/*
 * shadowhook_hook_sym_name — hook by symbol name inside a .so.
 *
 *   lib_name:   "libssl.so", "libc.so", etc. (basename)
 *   sym_name:   exported symbol name
 *   hook_func:  my replacement
 *   orig:       receives original function pointer
 */
void *shadowhook_hook_sym_name(const char *lib_name,
                               const char *sym_name,
                               void       *hook_func,
                               void      **orig);

/* shadowhook_unhook — remove hook and restore original bytes. */
int shadowhook_unhook(void *stub);

/* shadowhook_get_return_address — inside a hook, get the original caller. */
void *shadowhook_get_return_address(void);

/* error codes */
int shadowhook_get_errno(void);
const char *shadowhook_to_errmsg(int error_number);

#ifdef __cplusplus
}
#endif
