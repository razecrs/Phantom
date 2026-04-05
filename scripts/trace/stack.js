// @layer java
// phantom trace/stack.js — print Java stack traces on demand
//
// Exposes:
//   stackNow()           — log the current thread's stack immediately
//   stackOn(cls, method) — log stack every time cls.method() is called

Java.perform(() => {

  /**
   * stackNow(label?)
   * Print the current Java thread's call stack to logcat.
   */
  globalThis.stackNow = function stackNow(label) {
    const tag = label || 'stackNow';
    const frames = Java.use('android.util.Log')
                       .getStackTraceString(Java.use('java.lang.Exception').$new());
    ph.log(`[stack/${tag}]\n${frames}`);
  };

  /**
   * stackOn(className, methodName, opts?)
   *   opts.filter  — only print stack if return value / arg matches
   *   opts.depth   — max frames (default: 20)
   *
   * Example:
   *   stackOn('android.app.Activity', 'onCreate');
   *   stackOn('com.example.Client', 'sendRequest');
   */
  globalThis.stackOn = function stackOn(className, methodName, opts) {
    opts = opts || {};

    let klass;
    try {
      klass = Java.use(className);
    } catch (e) {
      ph.warn(`[stack] class not found: ${className}`);
      return;
    }

    const Exception = Java.use('java.lang.Exception');
    const Log       = Java.use('android.util.Log');

    try {
      const overloads = klass[methodName].overloads;
      overloads.forEach(ov => {
        ov.implementation = function(...args) {
          const frames = Log.getStackTraceString(Exception.$new());
          ph.log(`[stack] ${className}.${methodName}\n${frames}`);
          return ov.call(this, ...args);
        };
      });
      ph.log(`[stack] watching ${className}.${methodName} (${overloads.length} overloads)`);
    } catch (e) {
      ph.warn(`[stack] failed to hook ${className}.${methodName}: ${e}`);
    }
  };

  ph.log('[trace/stack] stackNow() and stackOn() available');
});
