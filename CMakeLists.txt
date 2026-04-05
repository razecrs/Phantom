cmake_minimum_required(VERSION 3.22)
project(phantom-core C ASM)

set(CMAKE_C_STANDARD 11)
set(ANDROID_ABI     ${ANDROID_ABI})

# ── compiler flags ─────────────────────────────────────────────
set(CORE_FLAGS "-O3 -fvisibility=hidden -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${CORE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

add_definitions(-DVERSION="2024-02-14")
add_definitions(-DCONFIG_VERSION="2024-02-14")

# ── arch-specific ASM flags ────────────────────────────────────
if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -march=armv8-a+crc+simd")
    set(ASM_SOURCES
        core/hook/arm64/trampoline.s
        core/scan/arm64/neon_scan.s
        core/ipc/arm64/atomic.s
    )
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -march=armv7-a")
    set(ASM_SOURCES
        core/hook/arm32/trampoline.s
        core/scan/arm32/neon_scan.s
        core/ipc/arm32/atomic.s
    )
endif()

# ── C sources ──────────────────────────────────────────────────
set(C_SOURCES
    core/jni_helper.c
    core/hook/trampoline.c
    core/hook/lsplant_stub.c
    core/hook/shadowhook_stub.c
    core/ipc/ringbuf.c
    core/js/runtime.c
    core/js/api_java.c
    core/js/api_intercept.c
    core/js/api_memory.c
    core/js/api_ph.c
    core/bypass/ssl_unpin.c
    core/bypass/root_hide.c
    core/bypass/integrity.c
    core/dex/dumper.c
    core/js/api_rhino.c
    core/js/api_network.c
    core/mitm/field_dict.c
    core/mitm/auto_detect.c
    core/mitm/ssl_tap.c
    core/mitm/http2_parse.c
    core/mitm/intercept_engine.c
    # QuickJS source files
    core/js/quickjs/quickjs.c
    core/js/quickjs/libregexp.c
    core/js/quickjs/libunicode.c
    core/js/quickjs/cutils.c
    core/js/quickjs/dtoa.c
)

add_library(phantom_core STATIC ${C_SOURCES} ${ASM_SOURCES})

target_include_directories(phantom_core PUBLIC
    core
    core/js/quickjs
)
