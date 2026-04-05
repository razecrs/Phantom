// @layer native
// phantom utils/hex.js — hex / bytes utilities for native scripts
//
// Exposes:
//   hex(addr, len)          — read `len` bytes at addr, return hex string
//   hexDump(addr, len)      — formatted hex dump (like xxd), returned as string
//   fromHex(hexStr)         — parse hex string → ArrayBuffer
//   toHex(arrayBuffer)      — ArrayBuffer → hex string

/**
 * hex(addr, len) — quick single-line hex string
 * Example:
 *   ph.log(hex(Module.findBaseAddress('libgame.so'), 16));
 */
globalThis.hex = function hex(addr, len) {
  const buf = Memory.readByteArray(addr, len);
  if (!buf) return '(null)';
  const bytes = new Uint8Array(buf);
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join(' ');
};

/**
 * hexDump(addr, len) — formatted dump
 * Output:
 *   0x12345678  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |........|
 */
globalThis.hexDump = function hexDump(addr, len) {
  const WIDTH = 16;
  const buf   = Memory.readByteArray(addr, len);
  if (!buf) return '(null)';
  const bytes = new Uint8Array(buf);
  const lines = [];

  for (let off = 0; off < bytes.length; off += WIDTH) {
    const slice = bytes.slice(off, off + WIDTH);
    const addrHex = (BigInt(addr) + BigInt(off)).toString(16).padStart(16, '0');

    const hex1 = Array.from(slice.slice(0, 8))
                      .map(b => b.toString(16).padStart(2, '0')).join(' ');
    const hex2 = Array.from(slice.slice(8))
                      .map(b => b.toString(16).padStart(2, '0')).join(' ');
    const ascii = Array.from(slice)
                       .map(b => (b >= 0x20 && b < 0x7f) ? String.fromCharCode(b) : '.')
                       .join('');

    lines.push(`0x${addrHex}  ${hex1.padEnd(23)}  ${hex2.padEnd(23)}  |${ascii}|`);
  }
  return lines.join('\n');
};

/**
 * fromHex(hexStr) — "deadbeef" → ArrayBuffer
 */
globalThis.fromHex = function fromHex(hexStr) {
  const clean = hexStr.replace(/\s/g, '');
  const buf   = new ArrayBuffer(clean.length / 2);
  const view  = new Uint8Array(buf);
  for (let i = 0; i < view.length; i++) {
    view[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16);
  }
  return buf;
};

/**
 * toHex(arrayBuffer) — ArrayBuffer → "deadbeef"
 */
globalThis.toHex = function toHex(buf) {
  return Array.from(new Uint8Array(buf))
              .map(b => b.toString(16).padStart(2, '0'))
              .join('');
};
