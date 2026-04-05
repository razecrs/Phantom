#include <jni.h>
#include <android/log.h>

__attribute__((visibility("default")))
void phantom_log(const char *tag, const char *msg) {
    __android_log_print(ANDROID_LOG_INFO, tag ? tag : "Phantom", "%s", msg);
}

/* Runs automatically when the SO is loaded into any process (dlopen or fork). */
__attribute__((constructor))
static void phantom_so_init(void) {
    __android_log_print(ANDROID_LOG_INFO, "Phantom",
        "SO loaded in pid=%d", (int)getpid());
}

/* I extract the JavaVM pointer from a JNIEnv — called once at module init. */
__attribute__((visibility("default")))
void *phantom_get_jvm(void *env_opaque) {
    JNIEnv *env = (JNIEnv *)env_opaque;
    JavaVM *jvm = NULL;
    (*env)->GetJavaVM(env, &jvm);
    return (void *)jvm;
}
