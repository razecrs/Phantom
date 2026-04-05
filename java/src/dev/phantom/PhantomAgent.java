package dev.phantom;

import org.mozilla.javascript.Context;
import org.mozilla.javascript.Function;
import org.mozilla.javascript.NativeJavaClass;
import org.mozilla.javascript.Scriptable;
import org.mozilla.javascript.ScriptableObject;

/**
 * I am the in-process Java agent loaded by Phantom via DexClassLoader.
 * I wrap Rhino so JS scripts tagged @layer java can call real Java APIs.
 *
 * Loaded at runtime from: /data/phantom/lib/phantom-agent.dex
 * Called from native via JNI: PhantomAgent.execRhino(String js)
 */
public class PhantomAgent {

    /**
     * Execute a JS script with full Java reflection access via Rhino.
     * Called from C via env->CallStaticVoidMethod after DexClassLoader loads us.
     */
    public static void execRhino(String script, String tag) {
        Context ctx = Context.enter();
        try {
            ctx.setOptimizationLevel(-1);          /* interpreter mode — works on Dalvik/ART */
            ctx.setLanguageVersion(Context.VERSION_ES6);

            Scriptable scope = ctx.initStandardObjects();

            /* expose Java.use(className) inside Rhino scripts */
            ScriptableObject.putProperty(scope, "Java", Context.javaToJS(new JavaBridge(), scope));

            /* expose ph.log(msg) */
            ScriptableObject.putProperty(scope, "ph", Context.javaToJS(new PhLog(tag), scope));

            ctx.evaluateString(scope, script, tag, 1, null);
        } finally {
            Context.exit();
        }
    }

    /** Bridge for Java.use(className) inside Rhino — returns a live class wrapper */
    public static class JavaBridge {
        public Object use(String className) throws ClassNotFoundException {
            Class<?> cls = Class.forName(className, true,
                    Thread.currentThread().getContextClassLoader());
            return new NativeJavaClass(
                    Context.getCurrentContext().initStandardObjects(), cls);
        }

        /** Java.perform(fn) — no-op wrapper for API compat with QuickJS layer */
        public void perform(Function fn) {
            fn.call(Context.getCurrentContext(),
                    fn.getParentScope(), fn.getParentScope(), new Object[0]);
        }
    }

    /** ph.log / ph.warn */
    public static class PhLog {
        private final String tag;
        PhLog(String tag) { this.tag = tag; }

        public void log(String msg) {
            android.util.Log.d("Phantom/" + tag, msg);
        }
        public void warn(String msg) {
            android.util.Log.w("Phantom/" + tag, msg);
        }
    }
}
