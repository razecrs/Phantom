#include "lsplant.h"
#include <android/log.h>

#define TAG "phantom/lsplant"

__attribute__((visibility("default")))
uint8_t lsplant_init(JNIEnv *env) {
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "lsplant_init: stub called — wire real LSPlant SO");
    (void)env;
    return 0;
}

__attribute__((visibility("default")))
uint8_t lsplant_hook(JNIEnv *env,
                     jobject target_method,
                     jobject hook_method,
                     jobject backup_method) {
    __android_log_print(ANDROID_LOG_WARN, TAG,
        "lsplant_hook: stub called — real LSPlant not loaded");
    (void)env; (void)target_method;
    (void)hook_method; (void)backup_method;
    return 0;
}

__attribute__((visibility("default")))
uint8_t lsplant_unhook(JNIEnv *env, jobject target_method) {
    (void)env; (void)target_method;
    return 0;
}

__attribute__((visibility("default")))
uint8_t lsplant_is_hooked(JNIEnv *env, jobject method) {
    (void)env; (void)method;
    return 0;
}
