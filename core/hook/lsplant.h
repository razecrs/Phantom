#pragma once
/*
 * lsplant.h — LSPlant C API declarations
 *
 * I include this wherever I need to hook Java methods via LSPlant.
 * The actual implementation comes from the pre-compiled LSPlant AAR/SO —
 * these are just the signatures I call into via JNI.
 *
 * LSPlant init must be called once from JNI_OnLoad before any hooks.
 * After that, lsplant_hook() replaces any ART method with my callback.
 */

#include <stdint.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * lsplant_init — call once from JNI_OnLoad.
 * Resolves libart symbols and prepares the hook engine.
 * Returns 1 on success, 0 on failure.
 */
uint8_t lsplant_init(JNIEnv *env);

/*
 * lsplant_hook — hook a Java method.
 *
 *   target_method:  jobject — the Method to hook (via reflection)
 *   hook_method:    jobject — my replacement Method
 *   backup_method:  jobject — receives the original Method so I can call through
 *
 * Returns 1 on success, 0 on failure.
 * All three are java.lang.reflect.Method objects obtained via JNI.
 */
uint8_t lsplant_hook(JNIEnv *env,
                     jobject target_method,
                     jobject hook_method,
                     jobject backup_method);

/*
 * lsplant_unhook — restore original method.
 * Pass the same target_method used in lsplant_hook.
 */
uint8_t lsplant_unhook(JNIEnv *env, jobject target_method);

/*
 * lsplant_is_hooked — check if a method is currently hooked.
 */
uint8_t lsplant_is_hooked(JNIEnv *env, jobject method);

#ifdef __cplusplus
}
#endif
