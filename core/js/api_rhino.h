#pragma once
#include <stddef.h>
#include <jni.h>

/*
 * Execute a JS script in Rhino (JVM-side) via DexClassLoader + PhantomAgent.
 * jvm   — JavaVM* captured from JNI_OnLoad or Zygisk post_app_specialize
 * script — JS source bytes
 * len    — length of script
 * tag    — filename / label for log output
 * Returns 0 on success, -1 on error.
 */
int phantom_exec_rhino(JavaVM *jvm, const char *script, size_t len, const char *tag);
