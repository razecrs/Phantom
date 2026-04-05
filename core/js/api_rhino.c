#include "api_rhino.h"
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

#define TAG "Phantom/Rhino"
#define AGENT_CLASS  "dev/phantom/PhantomAgent"
#define AGENT_DEX    "/data/phantom/lib/phantom-agent.dex"
#define AGENT_OPT    "/data/phantom/lib/opt"
#define RHINO_DEX    "/data/phantom/lib/rhino.jar"

/*
 * I load PhantomAgent via DexClassLoader then call execRhino(String, String).
 * jvm is the JavaVM* captured during Zygisk's JNI_OnLoad.
 */
int phantom_exec_rhino(JavaVM *jvm, const char *script, size_t len, const char *tag) {
    JNIEnv *env = NULL;
    int attached = 0;

    jint rc = (*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED) {
        if ((*jvm)->AttachCurrentThread(jvm, &env, NULL) != JNI_OK) return -1;
        attached = 1;
    } else if (rc != JNI_OK) {
        return -1;
    }

    /* bootstrap DexClassLoader to load our agent DEX */
    jclass ClassLoader = (*env)->FindClass(env, "java/lang/ClassLoader");
    jmethodID getSys = (*env)->GetStaticMethodID(env, ClassLoader,
            "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    jobject sysLoader = (*env)->CallStaticObjectMethod(env, ClassLoader, getSys);

    jclass DexClassLoader = (*env)->FindClass(env, "dalvik/system/DexClassLoader");
    jmethodID ctor = (*env)->GetMethodID(env, DexClassLoader, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");

    /* dex path = agent DEX + rhino JAR, colon-separated */
    char dex_path[512];
    snprintf(dex_path, sizeof(dex_path), "%s:%s", AGENT_DEX, RHINO_DEX);

    jstring jDexPath  = (*env)->NewStringUTF(env, dex_path);
    jstring jOptDir   = (*env)->NewStringUTF(env, AGENT_OPT);

    jobject loader = (*env)->NewObject(env, DexClassLoader, ctor,
            jDexPath, jOptDir, NULL, sysLoader);

    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        __android_log_print(ANDROID_LOG_ERROR, TAG, "DexClassLoader failed — is phantom-agent.dex installed?");
        if (attached) (*jvm)->DetachCurrentThread(jvm);
        return -1;
    }

    /* load PhantomAgent class */
    jmethodID findClass = (*env)->GetMethodID(env, DexClassLoader,
            "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring jClassName = (*env)->NewStringUTF(env, "dev.phantom.PhantomAgent");
    jclass AgentClass = (jclass)(*env)->CallObjectMethod(env, loader, findClass, jClassName);

    if (!AgentClass || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Failed to load PhantomAgent class");
        if (attached) (*jvm)->DetachCurrentThread(jvm);
        return -1;
    }

    jmethodID execMethod = (*env)->GetStaticMethodID(env, AgentClass,
            "execRhino", "(Ljava/lang/String;Ljava/lang/String;)V");

    jstring jScript = (*env)->NewStringUTF(env, script);
    jstring jTag    = (*env)->NewStringUTF(env, tag ? tag : "script");

    (*env)->CallStaticVoidMethod(env, AgentClass, execMethod, jScript, jTag);

    int err = (*env)->ExceptionCheck(env);
    if (err) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }

    (*env)->DeleteLocalRef(env, jScript);
    (*env)->DeleteLocalRef(env, jTag);
    (*env)->DeleteLocalRef(env, jClassName);
    (*env)->DeleteLocalRef(env, jDexPath);
    (*env)->DeleteLocalRef(env, jOptDir);

    if (attached) (*jvm)->DetachCurrentThread(jvm);
    return err ? -1 : 0;
}
