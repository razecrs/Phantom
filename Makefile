# phantom platform — top-level build
PHANTOM_PREFIX ?= /data/phantom
NDK            ?= C:/Users/raze/ndk/android-ndk-r27c
ABI_64         := arm64-v8a
ABI_32         := armeabi-v7a

# Windows specific
NDK_BIN        := $(NDK)/toolchains/llvm/prebuilt/windows-x86_64/bin
CLANG_64       := $(NDK_BIN)/aarch64-linux-android26-clang.cmd
CLANG_32       := $(NDK_BIN)/armv7a-linux-androideabi26-clang.cmd

# Android Platform Tools (adb, fastboot)
# Install: https://developer.android.com/tools/releases/platform-tools
# Then set ADB_DIR to the platform-tools directory, or add it to PATH yourself.
ADB_DIR ?= C:/Users/raze/AppData/Local/Android/Sdk/platform-tools
ADB     := $(ADB_DIR)/adb.exe
export PATH := $(ADB_DIR):$(PATH)

.PHONY: all core module shell go apk push kmod test-tramp pack clean

all: core module shell go

# ── C + ASM core (via CMake) ───────────────────────────────────
core:
	cmake -B build/arm64 -G "Unix Makefiles" -DANDROID_ABI=$(ABI_64) \
	      -DCMAKE_TOOLCHAIN_FILE=$(NDK)/build/cmake/android.toolchain.cmake \
	      -DCMAKE_MAKE_PROGRAM=$(NDK)/prebuilt/windows-x86_64/bin/make.exe \
	      -DANDROID_PLATFORM=android-26 .
	cmake --build build/arm64 --target phantom_core -j4

	cmake -B build/arm32 -G "Unix Makefiles" -DANDROID_ABI=$(ABI_32) \
	      -DCMAKE_TOOLCHAIN_FILE=$(NDK)/build/cmake/android.toolchain.cmake \
	      -DCMAKE_MAKE_PROGRAM=$(NDK)/prebuilt/windows-x86_64/bin/make.exe \
	      -DANDROID_PLATFORM=android-26 .
	cmake --build build/arm32 --target phantom_core -j4

CARGO     := C:/Users/raze/.cargo/bin/cargo.exe
# Force the GNU toolchain's rustc so Android cross-targets are found.
# VS Code installs an MSVC rustc earlier in PATH which shadows the
# correct one; RUSTC overrides cargo's toolchain resolution.
RUSTC_GNU := C:/Users/uruser/.rustup/toolchains/stable-x86_64-pc-windows-gnu/bin/rustc.exe
export RUSTC := $(RUSTC_GNU)

# ── Rust module — linkers/AR in module/.cargo/config.toml ─────
module: core
	cd module && RUSTC="$(RUSTC_GNU)" $(CARGO) build --release --target aarch64-linux-android
	cd module && RUSTC="$(RUSTC_GNU)" $(CARGO) build --release --target armv7-linux-androideabi

# ── psh shell ─────────────────────────────────────────────────
shell:
	mkdir -p build
	$(CLANG_64) -O3 -Ishell -o build/psh_arm64 shell/psh.c shell/prompt.c
	$(CLANG_32) -O3 -mthumb -Ishell -o build/psh_arm32 shell/psh.c shell/prompt.c

# ── Go binaries ───────────────────────────────────────────────
go:
	cd go/hub       && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../../build/phantom-hub_arm64 .
	cd go/lsp       && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../../build/phantom-lsp_arm64 .
	cd go/updater   && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../../build/phantom-updater_arm64 .
	cd go/bundler   && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../../build/phantom-bundler_arm64 .
	cd go/daemon    && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../../build/phantom-daemon_arm64 .
	cd go/bootstrap && GOARCH=arm64 GOOS=android go build -ldflags="-s -w" -o ../../build/phantom-bootstrap_arm64 .

# ── Kotlin companion app ──────────────────────────────────────
# Or open the root dir in Android Studio and click Run
apk:
	gradlew.bat assembleDebug
	@echo "APK: app/build/outputs/apk/debug/app-debug.apk"

# ── push module + tools to device via adb ─────────────────────
push:
	$(ADB) devices
	$(ADB) push build/phantom-module.zip /sdcard/phantom-module.zip
	@echo "Flash in Magisk/KSU app: Modules → Install from storage"

push-apk:
	$(ADB) install -r app/build/outputs/apk/debug/app-debug.apk

# ── forward daemon port so companion app works from PC ─────────
forward:
	$(ADB) forward tcp:7777 tcp:7777
	@echo "http://localhost:7777/status"

# ── trampoline unit test — built as ARM64 Android binary, run on device ──
test-tramp:
	mkdir -p build
	$(CLANG_64) -O0 -g -Icore core/test_tramp.c core/hook/trampoline.c \
	    core/hook/arm64/trampoline.s -o build/test_tramp_arm64
	@echo "Push with: adb push build/test_tramp_arm64 /data/local/tmp/ && adb shell chmod +x /data/local/tmp/test_tramp_arm64 && adb shell /data/local/tmp/test_tramp_arm64"

# ── package flashable zip ─────────────────────────────────────
pack: all
	bash pack.sh

clean:
	if exist build rmdir /s /q build
