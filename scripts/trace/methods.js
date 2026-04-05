// @layer java
// phantom trace/methods.js — live Java method call tracer
//
// Usage (from psh):
//   ph hook trace/methods.js
//
// Or in a script:
//   // @import ./trace/methods.js
//
// Traces every method call on a class and logs args + return value.
// Configure TRACE_CLASSES below or call trace() from your own script.

Java.perform(() => {

  /**
   * trace(className, opts?)
   *   opts.depth     — max stack depth in log (default: 4)
   *   opts.filter    — only trace methods matching this substring
   *   opts.logArgs   — log argument values (default: true)
   *   opts.logReturn — log return values (default: true)
   *
   * Example:
   *   trace('com.example.game.ShopManager');
   *   trace('com.example.game.AuthClient', { filter: 'verify' });
   */
  globalThis.trace = function traceClass(className, opts) {
    opts = opts || {};
    const depth     = opts.depth     !== undefined ? opts.depth     : 4;
    const filter    = opts.filter    || null;
    const logArgs   = opts.logArgs   !== undefined ? opts.logArgs   : true;
    const logReturn = opts.logReturn !== undefined ? opts.logReturn : true;

    let klass;
    try {
      klass = Java.use(className);
    } catch (e) {
      ph.warn(`[trace] class not found: ${className}`);
      return;
    }

    const methods = klass.class.getDeclaredMethods();
    let count = 0;

    methods.forEach(m => {
      const name = m.getName();
      if (filter && !name.includes(filter)) return;

      try {
        const overloads = klass[name].overloads;
        overloads.forEach(ov => {
          ov.implementation = function(...args) {
            const argStr = logArgs
              ? args.map(a => {
                  try { return JSON.stringify(a); } catch(_) { return String(a); }
                }).join(', ')
              : `(${args.length} args)`;

            ph.log(`[trace] ${className}.${name}(${argStr})`);

            const ret = ov.call(this, ...args);

            if (logReturn && ret !== undefined && ret !== null) {
              let retStr;
              try { retStr = JSON.stringify(ret); } catch(_) { retStr = String(ret); }
              ph.log(`[trace]   → ${retStr}`);
            }
            return ret;
          };
          count++;
        });
      } catch(_) {}
    });

    ph.log(`[trace] ${className}: tracing ${count} overloads`);
  };

  /**
   * traceAll(className)
   * Convenience: trace all methods including constructors.
   */
  globalThis.traceAll = function traceAll(className) {
    return globalThis.trace(className, { logArgs: true, logReturn: true });
  };

  ph.log('[trace/methods] trace() and traceAll() available');
});
