/// FFI into phantom-core (C static library)
use std::os::raw::{c_char, c_int, c_void};

extern "C" {
    // QuickJS runtime — @layer native scripts
    // script_name is used as bytecode cache key — pass actual filename
    pub fn phantom_exec_quickjs(js: *const c_char, len: usize,
                                pid: c_int, script_name: *const c_char) -> c_int;

    // Rhino runtime — @layer java scripts (needs JavaVM*)
    pub fn phantom_exec_rhino(jvm: *mut c_void, js: *const c_char, len: usize, tag: *const c_char) -> c_int;

    // Extract JavaVM* from a JNIEnv* — call once in post_app_specialize
    pub fn phantom_get_jvm(env: *mut c_void) -> *mut c_void;

    // LSPlant (Java side)
    pub fn lsplant_init(env: *mut c_void) -> c_int;

    // Ring buffer IPC
    pub fn ringbuf_create(path: *const c_char) -> *mut c_void;
    pub fn ringbuf_open(path: *const c_char) -> *mut c_void;
    pub fn ringbuf_write(rb: *mut c_void, data: *const c_void, len: usize) -> c_int;

    // SSL tap — hooks SSL_read/SSL_write for MITM traffic capture
    pub fn ssl_tap_init(rb: *mut c_void) -> c_int;
    pub fn ssl_tap_shutdown();

    // SSL unpin — supplemental hooks (SSL_CTX_set_verify, X509_verify_cert)
    pub fn phantom_ssl_unpin_init();

    // Root hiding — libc.access / stat / open hooks
    pub fn phantom_root_hide_init();

    // Integrity bypass — __system_property_get spoof
    pub fn phantom_integrity_init();

    // DEX dumper
    pub fn phantom_dex_dump(output_dir: *const c_char) -> c_int;

    // ShadowHook engine — must be called once before any hook installs
    // mode: 0 = unique (one hook per address), 1 = shared (hook chain)
    pub fn shadowhook_init(mode: c_int, debuggable: c_int) -> c_int;

    // Thin logcat helper used for diagnostic logging from Rust
    pub fn phantom_log(tag: *const c_char, msg: *const c_char);
}
