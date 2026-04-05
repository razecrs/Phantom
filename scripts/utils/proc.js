// @layer native
// phantom utils/proc.js — process / module introspection utilities
//
// Exposes:
//   listModules()           — log all loaded .so files with base + size
//   findModule(name)        — find a loaded module by partial name
//   findSymbol(so, sym)     — dlsym wrapper with null-check logging
//   mapRange(addr)          — find which module contains addr

/**
 * listModules() — print all loaded .so modules to logcat
 * Example:
 *   listModules();
 */
globalThis.listModules = function listModules() {
  const mods = Process.enumerateModules();
  ph.log(`[proc] ${mods.length} modules loaded:`);
  mods.forEach(m => {
    ph.log(`  ${m.name.padEnd(40)} base=0x${m.base.toString(16).padStart(16,'0')}`);
  });
  return mods;
};

/**
 * findModule(name) — find by partial name (case-insensitive)
 * Returns {name, base, size} or null
 * Example:
 *   const m = findModule('game');
 *   if (m) ph.log(`libgame base: 0x${m.base.toString(16)}`);
 */
globalThis.findModule = function findModule(name) {
  const lower = name.toLowerCase();
  const mods  = Process.enumerateModules();
  const hit   = mods.find(m => m.name.toLowerCase().includes(lower));
  if (!hit) {
    ph.warn(`[proc] module not found: ${name}`);
    return null;
  }
  ph.log(`[proc] ${hit.name} @ 0x${hit.base.toString(16)}`);
  return hit;
};

/**
 * findSymbol(soName, symbolName) — like Module.findExportByName with logging
 * Returns BigInt address or null
 */
globalThis.findSymbol = function findSymbol(soName, symbolName) {
  const addr = Module.findExportByName(soName, symbolName);
  if (addr === null || addr === 0n) {
    ph.warn(`[proc] ${symbolName} not found in ${soName}`);
    return null;
  }
  ph.log(`[proc] ${soName}!${symbolName} = 0x${addr.toString(16)}`);
  return addr;
};

/**
 * mapRange(addr) — find which module contains `addr`
 * Returns {name, base, size, offset} or null
 */
globalThis.mapRange = function mapRange(addr) {
  const mods = Process.enumerateModules();
  for (const m of mods) {
    const base = m.base;
    const end  = base + BigInt(m.size || 0x100000);
    const a    = BigInt(addr);
    if (a >= base && a < end) {
      const offset = a - base;
      ph.log(`[proc] 0x${a.toString(16)} → ${m.name}+0x${offset.toString(16)}`);
      return { name: m.name, base, size: m.size, offset };
    }
  }
  ph.warn(`[proc] 0x${BigInt(addr).toString(16)} not in any mapped module`);
  return null;
};
