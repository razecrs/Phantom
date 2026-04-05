use std::env;

fn main() {
    let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap();

    // tell rustc where to find libphantom_core.a (built by CMake)
    let root = env::var("CARGO_MANIFEST_DIR").unwrap();
    match target_arch.as_str() {
        "aarch64" => {
            println!("cargo:rustc-link-search=native={}/../build/arm64", root);
        }
        "arm" => {
            println!("cargo:rustc-link-search=native={}/../build/arm32", root);
        }
        _ => {}
    }

    // DON'T link phantom_core here, we will merge everything in the bridge
    // println!("cargo:rustc-link-lib=static=phantom_core");

    // compile EVERYTHING into one static lib via cc-rs
    let mut build = cc::Build::new();
    build
        .files([
            "../core/jni_helper.c",
            "../core/hook/lsplant_stub.c",
            "../core/hook/shadowhook_stub.c",
            "../core/ipc/ringbuf.c",
            "../core/js/runtime.c",
            "../core/js/api_ph.c",
            "../core/js/api_java.c",
            "../core/js/api_intercept.c",
            "../core/js/api_memory.c",
            "../core/bypass/ssl_unpin.c",
            "../core/bypass/root_hide.c",
            "../core/bypass/integrity.c",
            "../core/dex/dumper.c",
            "../core/js/api_rhino.c",
            "../core/js/api_network.c",
            "../core/mitm/field_dict.c",
            "../core/mitm/auto_detect.c",
            "../core/mitm/ssl_tap.c",
            "../core/mitm/http2_parse.c",
            "../core/mitm/intercept_engine.c",
            "../core/js/quickjs/quickjs.c",
            "../core/js/quickjs/libregexp.c",
            "../core/js/quickjs/libunicode.c",
            "../core/js/quickjs/cutils.c",
            "../core/js/quickjs/dtoa.c",
        ])
        .include("../core")
        .include("../core/js/quickjs")
        .flag("-O3")
        .define("VERSION", "\"2024-02-14\"")
        .define("CONFIG_VERSION", "\"2024-02-14\"");

    // arch-specific ASM
    match target_arch.as_str() {
        "aarch64" => {
            build.files([
                "../core/hook/arm64/trampoline.s",
                "../core/scan/arm64/neon_scan.s",
                "../core/ipc/arm64/atomic.s",
            ]);
            build.flag("-march=armv8-a+crc+simd");
        }
        "arm" => {
            build.files([
                "../core/hook/arm32/trampoline.s",
                "../core/scan/arm32/neon_scan.s",
                "../core/ipc/arm32/atomic.s",
            ]);
            build.flag("-march=armv7-a");
            build.flag("-mthumb");
        }
        _ => {}
    }

    build.compile("phantom");

    println!("cargo:rerun-if-changed=../core");
}
