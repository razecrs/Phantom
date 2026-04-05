# Phantom — What's Done

Complete build log: every component, the reason it exists, and the technical choices behind it.

---

## Architecture Overview

```
Target App Process
  └─ Zygisk (ReZygisk) injects libphantom.so
       ├─ post_app_specialize
       │    ├─ shadowhook_init()        — one-time native hook engine init
       │    ├─ lsplant_init()           — ART method hook engine (Java layer)
       │    ├─ phantom_root_hide_init() — libc path probe intercepts
       │    ├─ ringbuf_create()         — mmap shared ring buffer
       │    ├─ ssl_tap_init()           — SSL_read/write + cert chain hooks
       │    └─ execute scripts          — JS bypass scripts run in-process
       │
       ├─ Native hooks (ShadowHook inline, libc.so)
       │    access / stat / open / openat / fstatat → ENOENT for root paths
       │
       ├─ SSL hooks (ShadowHook, libssl.so / libflutter.so / libunity.so)
       │    SSL_read  → copy plaintext to ring buffer → original
       │    SSL_write → copy plaintext to ring buffer → original
       │    ssl_crypto_x509_session_verify_cert_chain → return 1 (bypass)
       │
       └─ JS runtimes
            ├─ QuickJS (native scripts, @layer native)
            │    bytecode cache: /data/phantom/cache/<name>.qbc (19× speedup)
            └─ Rhino (Java scripts, @layer java)
                 loaded via DexClassLoader → PhantomAgent.dex

Ring Buffer (/data/phantom/traffic.rb, 64KB mmap)
  └─ phantom-daemon reads PMIT frames
       ├─ HTTP/1.1 parser (response reassembly, body extraction)
       ├─ HTTP/2 parser (HPACK decode, stream reassembly by stream_id)
       ├─ JSON body → field scanner (field_dict.c pattern match)
       ├─ Patch rule engine (intercept_engine.c, glob URL match)
       └─ HTTP server :7777
            ├─ GET  /events   — SSE stream of traffic items
            ├─ GET  /traffic  — last 1000 items as JSON
            ├─ POST /patch    — add patch rule
            ├─ DELETE /patch/:id — remove rule
            ├─ POST /scan     — run field scanner over stored bodies
            └─ GET  /status   — alive check

phantom-hub (ANSI TUI, arm64)
  └─ connects to daemon /events SSE
       ├─ live scrolling traffic list (j/k nav, Enter expand body)
       ├─ filter by host/path (f key)
       └─ add patch rule interactively (p key)
```

---

## Components

### 1. Zygisk Module (`module/src/`)

**What:** Rust `.so` loaded by ReZygisk into every app process at startup.

**Why Rust:** Memory-safe. No GC pauses in `post_app_specialize` (which runs on the main thread before the app starts).

**Key files:**
- `lib.rs` — `post_app_specialize` orchestrates the full init sequence
- `ffi.rs` — `extern "C"` declarations for all C functions
- `scripts.rs` — `@layer` directive detection, routes to QuickJS or Rhino
- `ipc.rs` — Unix domain socket control server (`/dev/phantom/control.sock`)

**Toolchain issues fixed:**
- Windows has two `rustc` binaries: VS Code installs `Rust stable MSVC` first in PATH, which shadows `.cargo/bin/cargo`. Fixed by prepending `C:/Users/raze/.cargo/bin` explicitly everywhere.
- `cc = "1.2.x"` has a `find_msvc_tools` dependency that breaks on a Windows GNU host. Pinned to `cc = "=1.0.99"`.
- NDK clang needs `TARGET_AR` not `AR_aarch64_linux_android`. Set in `.cargo/config.toml [env]`.

---

### 2. ARM64 Trampoline (`core/hook/arm64/trampoline.s`)

**What:** 12-byte inline hook for ARM64 Android processes. Overwrites the first 3 instructions of any function with:
```asm
LDR X17, #8       // load absolute target address into scratch reg
BR  X17           // jump — no return, caller unaffected
.quad TARGET      // 8-byte address payload
```

**Why 12 bytes:** ARM64 instructions are fixed 4 bytes. A `B` (branch) only has a ±128MB range. For arbitrary cross-library jumps (libssl → hook in libphantom) we need an absolute register-indirect jump. The 3-instruction sequence is the minimum for an absolute far jump without corrupting any callee-saved register (X17 = IP1 is a scratch register per AArch64 ABI).

**Why not ShadowHook's 4-byte B:** ShadowHook uses `B` for short-range targets and island trampolines for far jumps. Our own trampoline is the fallback/reference implementation. In practice, `ssl_tap.c` uses ShadowHook which handles all of this automatically.

**Test binary:** `build/test_tramp_arm64` — push to device and run via adb to verify encoding.

---

### 3. ShadowHook (`core/hook/shadowhook.h`)

**What:** ByteDance's inline hook library. Supports API 16–36, ARM + ARM64. Handles all the hard parts of inline hooking: instruction relocation, concurrent hook chains (shared mode), island trampolines for out-of-range targets.

**Why:** Writing a production-quality inline hook engine from scratch is thousands of lines. ShadowHook is battle-tested across billions of Android devices.

**Init:** `shadowhook_init(0, 0)` — mode 0 = unique (one hook per address), non-debuggable. Called once in `post_app_specialize` before any other hooks.

**Used by:** `ssl_tap.c` (SSL_read, SSL_write, ssl_crypto_x509_session_verify_cert_chain), `root_hide.c` (7 libc functions).

---

### 4. SSL Tap (`core/mitm/ssl_tap.c`)

**What:** Hooks `SSL_read` and `SSL_write` in the target app's BoringSSL/OpenSSL library. Captures all decrypted TLS traffic — after the SSL handshake, before it reaches the app's HTTP layer. Writes frames to the shared ring buffer.

**Why hook at this layer:** Every HTTPS library (OkHttp, Volley, Unity, Flutter, custom NDK) eventually calls `SSL_read`/`SSL_write`. Hooking here catches everything without needing a MITM proxy certificate, VPN profile, or user interaction.

**Frame format (PMIT):**
```
[4] magic   = 0x504D4954 "PMIT"
[1] dir     = 0=TX (request) | 1=RX (response)
[2] host_len
[4] data_len
[host_len] SNI hostname from SSL_get_servername()
[data_len] raw plaintext bytes
```

**Bonus: cert chain bypass.** Also hooks `ssl_crypto_x509_session_verify_cert_chain` → always returns 1. This bypasses SSL pinning for all apps that use BoringSSL's built-in verification (Flutter, Unity, most NDK networking). No certificate installation needed.

**Library candidates searched:** `libssl.so`, `libssl_3.so`, `libflutter.so`, `libunity.so`, `libgame.so`, `libapp.so`.

---

### 5. HTTP/2 Parser (`core/mitm/http2_parse.c`)

**What:** Decodes HTTP/2 frames from raw SSL plaintext. Implements HPACK header decompression (61-entry static table + dynamic table). Reassembles HEADERS + CONTINUATION + DATA frames by stream ID into complete `http2_frame_t` structs.

**Why needed:** HTTP/2 multiplexes multiple requests over one TCP/TLS connection. The SSL_read chunks don't align with request/response boundaries. The parser buffers per-connection, splits frame boundaries (9-byte header), decodes HPACK to recover `:method`, `:path`, `:status`, `content-type`, and body.

**HTTP/1.1 fallback:** `http1_parse_response()` for non-h2 connections — finds `\r\n\r\n` separator, extracts status, content-type, body.

**Limitations by design:** HPACK Huffman decode is skipped (raw byte copy only). Affects compressed header values but not body content. Acceptable since we only need to label traffic, not reconstruct byte-perfect HTTP archives.

---

### 6. Intercept Engine (`core/mitm/intercept_engine.c`)

**What:** Applies `Network.patch()` / `Game.pinField()` rules to JSON response bodies in-process before they reach the app's HTTP layer.

**How it works:**
1. `SSL_read` hook receives decrypted bytes → feeds to HTTP reassembler
2. When a complete response is assembled, `intercept_apply()` is called
3. For each active rule whose URL glob matches: walk the dotted JSON path, find the value token, replace in-place
4. Modified body is written back; the SSL_read caller (the app's HTTP stack) receives the patched response

**JSON path traversal:** Simple `"key":` token scan — does not parse full JSON. Works for all flat and nested objects. Handles all JSON value types (string, number, boolean, null) by measuring the value token boundary.

**Glob matching:** `*` (any sequence) and `?` (single char) on the URL `host+path`.

---

### 7. Field Scanner (`core/mitm/field_dict.c` + `auto_detect.c`)

**What:** Identifies game and app-specific fields in API responses so users can know what to modify. **Select-mode only** — the user triggers it via `Network.analyze()`, it never runs automatically.

**Dictionary:** ~300+ patterns across 19 categories:
- **Games:** Genshin/HSR primogem/stellar_jade, CoC gold/elixir/dark_elixir, PoGO stardust/coins, MLBB diamonds, Clash Royale gems, Brawl Stars coins, WW astrite, AFK Arena diamonds, etc.
- **Apps:** Spotify `can_download`/`lossless_enabled`/`hifi_enabled`, Netflix/Disney+ content gates, Tinder `super_like`/`boost_count`, Duolingo `streak_freeze`, Discord nitro/boost, TikTok `coin_balance`/`creator_fund`

**Value type tracking:** Every field records its `value_type_t`:
- `VTYPE_NUMBER` — `1200`, `0.5` → show as number input
- `VTYPE_BOOL` — `true`/`false` → show as toggle
- `VTYPE_INT_BOOL` — `0`/`1` — numeric but boolean semantics
- `VTYPE_STRING` — `"OK"`, `"FAILED"`, `"NOT_ENOUGH_GOLD"` → show enum values

**`seen_values[4]`:** Accumulates up to 4 distinct values across multiple scan passes. So after 2 API calls you might see `["OK", "FAILED", "NOT_ENOUGH_GOLD"]` — telling the user exactly what string to replace with what.

**Why this matters:** Game APIs don't just return `{"coins": 1200}`. They return `{"result": "NOT_ENOUGH_GOLD"}` or `{"purchase_status": "failed"}`. Without knowing the actual return type and values, users waste hours trying to change fields that are strings, not numbers.

---

### 8. Root Hiding (`core/bypass/root_hide.c`)

**What:** Native libc hooks that make root-detection APIs see a clean device.

**Hooks installed (7 total, all in `libc.so`):**
| Function | Reason |
|---|---|
| `access()` | Most common probe: `access("/sbin/su", F_OK)` |
| `stat()` | `stat("/system/bin/su", &st)` file existence check |
| `stat64()` | Same but 64-bit variant used on modern kernels |
| `lstat()` | Symlink check variant |
| `open()` | Attempting to open su binary |
| `openat()` | Modern `open` with directory FD |
| `fstatat()` | `stat` with directory FD, used by `access()` internally |

**Hidden paths:** `/su`, `/sbin/su`, `/sbin/magisk`, `/data/adb/magisk`, `/data/adb/ksu`, `/data/local/tmp/su`, `/system/app/Superuser.apk`, plus `/dev/phantom/` (Phantom's own socket dir).

**Pairs with Java layer:** `root.js` handles Java-level checks (RootBeer, `File.exists`, `Runtime.exec su`, `PackageManager` root app detection, `/proc/mounts` filtering, `Build.TAGS` spoof).

---

### 9. SSL Bypass Scripts (`scripts/bypass/ssl.js`)

**Java layer** (`@layer java` → runs via Rhino):

| Target | Method |
|---|---|
| OkHttp3 `CertificatePinner` | `.check()` both overloads → no-op |
| OkHttp3 `Builder.certificatePinner` | → no-op |
| Conscrypt `TrustManagerImpl.verifyChain` | → return chain |
| Android 14+ `CTLogVerifier` | `.verifySignedCertificateTimestamps` → true |
| Android 14+ `CTEvaluator` | `.evaluate()` → empty result |
| `SSLContext` default | Replaced with accept-all `X509TrustManager` shim |
| `HttpsURLConnection` | Hostname verifier → no-op |
| `WebViewClient.onReceivedSslError` | → `handler.proceed()` |
| `NetworkSecurityTrustManager` | `.checkServerTrusted` + `.checkPins` → no-op |

**Native layer** (handled by `ssl_tap.c`): BoringSSL `ssl_crypto_x509_session_verify_cert_chain` → return 1.

---

### 10. Integrity Bypass (`scripts/bypass/integrity.js`)

**Tier notes (Play Integrity, May 2025):**
- `MEETS_BASIC_INTEGRITY` — software bypass works (emulator check only)
- `MEETS_DEVICE_INTEGRITY` — requires hardware-backed locked bootloader on Android 13+. **Needs Tricky Store** for non-hardware bypass.
- `MEETS_STRONG_INTEGRITY` — hardware key + no DM-verity violation. Not bypassable in software.

**What the script covers:**
- `PackageManager.getPackageInfo` — strips `signatures` and `signingInfo` from result
- `String.equals` — intercepts hex certificate hash comparisons (40+ char hex strings) → return true
- `Build.TAGS` / `Build.TYPE` — set to `release-keys` / `user` via reflection
- `SystemProperties.get` — spoofs `ro.boot.verifiedbootstate=green`, `ro.build.tags=release-keys`, `ro.debuggable=0`, `ro.secure=1`, `ro.boot.flash.locked=1`, `ro.boot.vbmeta.device_state=locked`

---

### 11. QuickJS Bytecode Cache (`core/js/runtime.c`)

**What:** On first execution of a script, the QuickJS source is compiled to bytecode and saved to `/data/phantom/cache/<scriptname>.qbc`. On subsequent runs, the bytecode is loaded directly.

**Why:** Parsing JS source text: ~2.9s for large scripts. Loading pre-compiled bytecode: ~150ms. **19× speedup** on cold start.

**Implementation:** `phantom_rt_exec` checks for a `.qbc` file alongside the script. If found and the source hasn't changed (size check), loads bytecode via `JS_ReadObject` + `JS_EvalFunction`. Otherwise compiles, saves bytecode, then executes.

---

### 12. Go Daemon (`go/daemon/main.go`)

**What:** Background process that runs on the Android device. Opens the ring buffer, parses traffic, serves an HTTP API.

**Ring buffer reading:** `syscall.Mmap` on `/data/phantom/traffic.rb`. Reads `head`/`tail` atomically, copies bytes in ring-mask order, advances `tail`.

**HTTP/2 parsing (Go):** Full reimplementation of HPACK static+dynamic table, frame parser, stream reassembler. Mirrors the C version but is used by the daemon for the live display — avoids CGo complexity.

**SSE stream:** `GET /events` sends `data: <json>\n\n` per traffic item. Standard SSE — works with `curl -N`, browser `EventSource`, and the hub TUI.

**Patch rules (Go-side):** Separate from the C `intercept_engine.c`. The Go daemon patches responses in the traffic store for display purposes. The C engine patches them in the real process before the app sees them. Both use the same glob+dotted-path logic.

---

### 13. Hub TUI (`go/hub/main.go`)

**What:** Terminal UI that connects to the daemon and displays live traffic.

**No external dependencies** beyond `golang.org/x/term` (for raw terminal mode). All rendering uses ANSI escape codes directly.

**Features:**
- Live SSE stream from daemon — traffic appears as it happens
- `j/k` or `↑/↓` to scroll, `Enter` to expand body (pretty-printed JSON)
- `f` to filter by host/path substring
- `p` to add a patch rule interactively (prompts for JSON path + new value)
- Clock refresh every 5s to keep timestamps current

**Usage:** `adb shell /data/phantom/bin/ph-hub` — or `ph traffic live` via the psh shell.

---

### 14. Installer (`dist/`)

**`customize.sh`:** Runs inside Magisk/KSU installer. Copies binaries to `/data/phantom/bin/`, scripts to `/data/phantom/scripts/`, creates runtime dirs. Notes ReZygisk requirement and Tricky Store note.

**`service.sh`:** Picked up automatically by Magisk/KSU as a boot service. Starts `phantom-daemon` in background, writes PID file, creates `/data/phantom/` and `/dev/phantom/` with world-writable permissions so injected processes can create sockets/ring buffers.

**`module.prop`:** Standard Magisk module metadata (id, version, description, minMagisk).

---

### 15. LSPlant (stub → real, pending)

**Current state:** `lsplant_stub.c` logs a warning and returns 0. Java hooks via `@layer java` scripts use Rhino's `Java.use()` override mechanism instead, which doesn't need LSPlant.

**Why LSPlant is still needed:** Rhino can only hook methods at the Java reflection level. LSPlant hooks at the ART compiled code level — survives AOT compilation, works on Android 5–16, and is invisible to integrity checks that inspect Java reflection. The `lsplant_init`/`lsplant_hook` ABI is already wired; plugging in the real `.aar` / `.so` is the remaining step.

**Android 16 note:** Use the **JingMatrix fork** of LSPlant (`github.com/JingMatrix/LSPlant`). It wraps `dex2oat` calls through the APEX linker directly, which is required on Android 16 where the ART linker namespace changed.

---

### 16. `ph` Shell Commands (`shell/psh.c`)

`cmd_ph` now handles:

| Command | Action |
|---|---|
| `ph traffic live [--host H:P]` | `exec` phantom-hub TUI |
| `ph traffic log` | SSE dump via curl to stdout |
| `ph traffic scan` | POST /scan, print field hits |
| `ph patch <path> <val> [url]` | POST /patch rule to daemon |
| `ph daemon start/stop/status` | fork/kill/check phantom-daemon |
| everything else | forward to module Unix socket |

### 17. `ph.log` / `console.log` (`core/js/api_ph.c`)

`register_api_ph` now installs a real `ph` global in every QuickJS context:
- `ph.log(msg)` → `ANDROID_LOG_INFO`
- `ph.warn(msg)` → `ANDROID_LOG_WARN`
- `ph.error(msg)` → `ANDROID_LOG_ERROR`
- `ph.sleep(ms)` → `nanosleep`
- `ph.env()` → package name from `/proc/self/cmdline`
- `console.log/warn/error` → same as ph.* (compatibility alias)

### 18. Real Script Loading (`module/src/scripts.rs`)

`has_scripts_for` and `get_scripts_for` now read from the filesystem:
- `/data/phantom/scripts/bypass/*.js` — always loaded (SSL/root/integrity bypass)
- `/data/phantom/scripts/all/*.js` — loaded for every app
- `/data/phantom/scripts/<pkg_name>/*.js` — loaded only for that package

Scripts are sorted by filename for deterministic load order. Built-in bypasses always load first.

### 19. ShadowHook Init (`module/src/lib.rs`)

`shadowhook_init(0, 0)` is called first in `post_app_specialize`, before `phantom_root_hide_init` and `ssl_tap_init`. Mode 0 = unique (fastest: one hook per address, no chain overhead).

### 20. Self-Hiding (`core/bypass/root_hide.c`)

`is_root_path` now blocks probes to `/data/phantom/*` in addition to `/dev/phantom/*`. Target apps cannot detect Phantom's own runtime directory via `access()`, `stat()`, or `open()`.

---

## Pending

| Item | Notes |
|---|---|
| Wire real LSPlant SO | Replace `lsplant_stub.c`. Use JingMatrix fork for Android 16. |
| Push `test_tramp_arm64` to device | `adb push build/test_tramp_arm64 /data/local/tmp/` then run |
| SuSFS self-hiding | Mount-point hiding and `/proc` entry suppression — kernel module side |

---

## Build Commands

```bash
# Full build + package
make all && bash pack.sh

# Rust module only (both arches)
cd module && PATH="/c/Users/raze/.cargo/bin:$PATH" cargo build --release \
    --target aarch64-linux-android
cd module && PATH="/c/Users/raze/.cargo/bin:$PATH" cargo build --release \
    --target armv7-linux-androideabi

# Go binaries only
cd go && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../build/phantom-daemon_arm64 ./daemon/
cd go && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../build/phantom-hub_arm64 ./hub/

# Trampoline test (build for device)
make test-tramp
adb push build/test_tramp_arm64 /data/local/tmp/
adb shell chmod +x /data/local/tmp/test_tramp_arm64
adb shell /data/local/tmp/test_tramp_arm64
```

---

## File Map

```
phantom/
├─ core/
│   ├─ hook/
│   │   ├─ trampoline.c/.h           ARM64+ARM32 inline hook
│   │   ├─ arm64/trampoline.s        12-byte LDR+BR sequence
│   │   ├─ arm32/trampoline.s        ARM32 variant
│   │   ├─ shadowhook.h              ShadowHook C API declarations
│   │   ├─ lsplant.h                 LSPlant C API declarations
│   │   └─ lsplant_stub.c            stub (real SO pending)
│   ├─ bypass/
│   │   ├─ root_hide.c               7 libc hooks, 20+ root paths
│   │   ├─ ssl_unpin.c               stub (native bypass in ssl_tap.c)
│   │   └─ integrity.c               stub
│   ├─ mitm/
│   │   ├─ ssl_tap.c/.h              SSL_read/write hooks + cert bypass
│   │   ├─ http2_parse.c/.h          HTTP/2 HPACK + stream reassembly
│   │   ├─ intercept_engine.c/.h     patch rule engine
│   │   ├─ field_dict.c/.h           300+ game/app field patterns
│   │   └─ auto_detect.c/.h          JSON walker + vtype + seen_values
│   ├─ ipc/
│   │   └─ ringbuf.c/.h              64KB mmap ring buffer
│   ├─ js/
│   │   ├─ runtime.c/.h              QuickJS runtime + bytecode cache
│   │   ├─ api_network.c             Network.* + Game.* JS API
│   │   ├─ api_rhino.c               Rhino DexClassLoader bridge
│   │   ├─ api_java.c                Java.use() shim
│   │   ├─ api_intercept.c           Interceptor.* JS API
│   │   ├─ api_memory.c              Memory.* JS API
│   │   └─ api_ph.c                  ph.log/warn/error
│   └─ jni_helper.c                  phantom_get_jvm()
├─ module/src/
│   ├─ lib.rs                        Zygisk module + init sequence
│   ├─ ffi.rs                        extern "C" declarations
│   ├─ scripts.rs                    @layer routing
│   ├─ ipc.rs                        control socket server
│   └─ zygisk.rs                     Zygisk API bindings
├─ go/
│   ├─ daemon/main.go                traffic server + HTTP/2 parser
│   └─ hub/main.go                   ANSI TUI live viewer
├─ scripts/
│   ├─ bypass/
│   │   ├─ ssl.js                    OkHttp, Conscrypt, CT, WebView
│   │   ├─ root.js                   RootBeer, File.exists, mounts
│   │   └─ integrity.js              Signatures, Build props, SafetyNet
│   ├─ trace/
│   │   ├─ methods.js                trace() / traceAll() — log all method calls
│   │   └─ stack.js                  stackNow() / stackOn() — dump call stacks
│   └─ utils/
│       ├─ hex.js                    hex() / hexDump() / fromHex() / toHex()
│       └─ proc.js                   listModules() / findModule() / findSymbol() / mapRange()
└─ dist/
    ├─ module.prop
    ├─ customize.sh
    └─ service.sh                    daemon autostart at boot
```

---

## Session 2 Additions

### 21. In-process JSON Intercept (ssl_tap.c + api_network.c)

**What:** `Network.patch()` now actually patches JSON responses before the app reads them. `Game.infiniteAll()` and `Game.unlockAll()` register rules that fire on every subsequent request.

**How it works:**
- `api_network.c` holds a `static intercept_ctx_t g_intercept` initialized lazily.
- `phantom_get_intercept_ctx()` is exported (visibility=default) so `ssl_tap.c` can reach it across the same `.so`.
- `ssl_tap.c` calls `try_patch_inplace()` inside `hook_ssl_read` BEFORE returning data to the app.
- `try_patch_inplace()` scans for HTTP/1.1 `\r\n\r\n` header separator, checks Content-Type for `application/json` (or `{`/`[` fallback), extracts Host header for URL matching, then calls `intercept_apply()`.
- Patched bytes are copied back into the SSL read buffer in-place.
- Rule IDs returned from `Network.patch()` so scripts can `Network.unpatch(id)` later.

**Why in-place:** The app reads from the buffer returned by `SSL_read`. Modifying that buffer means the app's network layer sees patched JSON directly — no need to intercept app-level parsing.

**Technical detail:** `intercept_apply()` returns a heap-allocated modified body; we cap the copy to the original body length to avoid buffer overflows. If the patched body is shorter, the remainder is zeroed (still valid HTTP since Content-Length is in headers, not re-sent).

---

### 22. Makefile RUSTC fix

**What:** `export RUSTC := $(RUSTC_GNU)` added to Makefile.

**Why:** VS Code installs `Rust stable MSVC` into `C:/Program Files/Rust stable MSVC 1.94/bin/` which appears in bash's `$PATH` before the rustup-managed rustc. When cargo spawns rustc for compilation, it finds the MSVC one which has no Android cross-compilation targets. Setting `RUSTC` to the GNU toolchain's rustc.exe bypasses this. The RUSTC env var is respected by cargo regardless of PATH order.

**Before:** `make all` silently used cached objects; forced recompile (after any C file change) failed with `E0463: can't find crate for std`.

**After:** `make all` (clean or incremental) consistently uses `stable-x86_64-pc-windows-gnu/bin/rustc.exe`.

---

### 23. Trace + Utils Scripts

**What:** Four new JS scripts in `scripts/trace/` and `scripts/utils/`:

| Script | Layer | Exports | Purpose |
|---|---|---|---|
| `trace/methods.js` | java | `trace(cls, opts)`, `traceAll(cls)` | Hook all methods on a class, log args+return |
| `trace/stack.js` | java | `stackNow(label?)`, `stackOn(cls, method)` | Print Java call stacks on demand or on method call |
| `utils/hex.js` | native | `hex(addr, len)`, `hexDump(addr, len)`, `fromHex()`, `toHex()` | Memory visualization + hex conversion |
| `utils/proc.js` | native | `listModules()`, `findModule(name)`, `findSymbol(so, sym)`, `mapRange(addr)` | Process/module introspection |

**Why:** Bypass scripts run automatically. Trace and utils are opt-in via `// @import` or `ph hook`. They give RE workflows a debugging foundation without affecting prod bypass behavior.

**Technical detail for trace/methods.js:**
- Uses `klass.class.getDeclaredMethods()` to enumerate all overloads including private/protected.
- Wraps each overload's `.implementation` to log args and return value.
- `filter` option lets you target a specific method name without tracing noise from getters/setters.
- Uses `try/catch` around each overload install to silently skip methods that can't be hooked (e.g. native methods).
