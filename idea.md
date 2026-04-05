# Phantom — Ideas & Timeline

> Every thought goes here. Nothing gets lost.
> Batches are ordered by necessity — I do Batch 0 before touching Batch 1.
> Within a batch, items are roughly ordered too. Don't rush.

---

## Batch 0 — Critical Path (MVP, nothing works without this)

- [x] Zygisk module skeleton compiles and loads via ZygiskNext
- [x] C core links statically into Rust module (build.rs + CMake wired)
- [x] ARM64 trampoline installs and uninstalls cleanly (test with a dummy target)
- [x] ARM32 Thumb-2 trampoline same
- [x] Ring buffer mmap works between module process and companion app
- [x] QuickJS embeds in C core, executes a hello-world JS from Rust
- [x] `ph attach <pkg>` talks to module via Unix socket — gets a response
- [x] `psh` compiles for aarch64 via NDK, launches on device
- [x] LSPlant C FFI wired — Java method hook fires in a test app
- [x] Arch ARM bootstrap runs, pacman works inside the rootfs chroot
- [x] `psh -S nodejs` installs Node from Arch ARM repo, binary runs

---

## Batch 1 — RE Essentials (The "Frida-lite" Layer)

- [x] `Java.use(className)` — basic class wrapper in JS
- [x] `Interceptor.attach(addr, { onEnter, onLeave })` — native hooks from JS
- [x] `Memory.readU32/writeU32/scan` — NEON-accelerated scanner bindings
- [x] `ph hook <script.js>` — load a script into a process on the fly
- [x] `ph trace <class>` — instant method tracer (auto-generate hooks for all methods)
- [x] DEX dumper — walk ClassLinker to dump in-memory DEX files
- [x] Rhino integration — execute JS on the JVM side (access to all Java objects)
- [x] SSL Unpinning + Root/Integrity bypass scripts (bundled)

---

## Batch 2 — Shell Experience (User Land)

- [ ] PSH: full command completion (tab) for both files and phantom commands
- [ ] PSH: status line showing attached app, current PID, and RE status
- [ ] LSP: basic Go-based LSP for `.js` scripts with Phantom API completions
- [ ] Hub: CLI tool to download/share phantom scripts (community repo)
- [ ] Bundler: bundle multiple JS/C files into a single `.ph` plugin

---

## Batch 3 — The GUI (Companion App)

- [ ] Interactive process list with search and "attach" button
- [ ] Live log viewer (streaming from ring buffer)
- [ ] Hex editor with live memory view + "follow in hex" from scan results
- [ ] Script manager — edit and toggle scripts with UI switches
- [ ] Plugin store — browse and install from Hub directly in app

---

## Root Modules (Core Architecture)

| Component | Tech | Responsibility |
| :--- | :--- | :--- |
| `phantom-core` | C11, ASM | Trampolines, QuickJS embedding, NEON scanner, IPC primitives |
| `phantom-module` | Rust (Zygisk) | App lifecycle hooking, process specialization, IPC server |
| `psh` | C, Go | Custom Arch-based shell with embedded RE commands |
| `phantom-app` | Kotlin (Compose) | UI, script management, log visualization, device-side hub |
| `kmod` | C (GKI) | Stealth memory access, bypassing `/proc/pid/mem` restrictions |

---

## Future & "Maybe"

| Date | Thought | Logic |
| :--- | :--- | :--- |
| 2026-04-03 | WASM support for hooks | Portable native-speed hooks? Maybe overkill if QuickJS is fast enough. |
| 2026-04-03 | Built-in decompiler | Port a tiny decompiler to C or Go for on-device droid code editor |
| 2026-04-03 | mmap ring buffer for log IPC | Zero-copy, no socket serialization overhead |
| 2026-04-03 | iOS — hard no for now | Too gated, signing hell, tiny jailbreak base. Android only for MVP and foreseeable future. |
