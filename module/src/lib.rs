mod zygisk;
mod process;
mod scripts;
mod ipc;
mod ffi;

use std::sync::OnceLock;
use std::sync::atomic::{AtomicBool, Ordering};
use zygisk::{ZygiskModule, ZygiskOption, JNIEnv, SyncPtr};

register_zygisk_module!(PhantomModule);

pub static G_JVM: OnceLock<SyncPtr<std::ffi::c_void>> = OnceLock::new();
pub static G_RB:  OnceLock<SyncPtr<std::ffi::c_void>> = OnceLock::new();

/* set in pre_app_specialize (root context) so post_app_specialize can open it */
static RB_CREATED: AtomicBool = AtomicBool::new(false);

const RB_PATH: &str = "/data/phantom/traffic.rb\0";

#[derive(Default)]
struct PhantomModule;

impl ZygiskModule for PhantomModule {
    /* ── pre: still running as root / zygote context ──────────────────── */
    fn pre_app_specialize(&self, _params: &mut zygisk::AppSpecializeArgs) {
        /* Create the ring buffer file now, while we still have root.
         * post_app_specialize runs in app SELinux context which can't
         * create files in /data/phantom/. */
        let rb = unsafe { ffi::ringbuf_create(RB_PATH.as_ptr() as *const _) };
        if !rb.is_null() {
            RB_CREATED.store(true, Ordering::Relaxed);
            /* store pointer so post can use it without re-opening */
            let _ = G_RB.set(SyncPtr(rb));
        }
    }

    /* ── post: running as app context — install hooks ─────────────────── */
    fn post_app_specialize(&self, params: &zygisk::AppSpecializeArgs, env: *mut JNIEnv) {
        let pkg = params.nice_name();
        let pid = unsafe { libc::getpid() };

        /* log that we fired */
        let tag  = b"Phantom\0".as_ptr() as *const std::os::raw::c_char;
        let msg  = format!("post_app_specialize: pkg={} pid={}\0", pkg, pid);
        unsafe { ffi::phantom_log(tag, msg.as_ptr() as *const _) };

        ipc::start_control_server(pkg);

        /* ShadowHook init before any hook installs */
        unsafe { ffi::shadowhook_init(0, 0) };

        /* install SSL tap using the ring buffer created in pre */
        if let Some(rb_ptr) = G_RB.get() {
            unsafe { ffi::ssl_tap_init(rb_ptr.0) };
        }

        /* supplemental SSL unpin */
        unsafe { ffi::phantom_ssl_unpin_init() };

        /* root hiding + integrity bypass */
        unsafe { ffi::phantom_root_hide_init() };
        unsafe { ffi::phantom_integrity_init() };

        /* JVM + LSPlant */
        if !env.is_null() {
            let jvm = unsafe { ffi::phantom_get_jvm(env as *mut _) };
            if !jvm.is_null() { let _ = G_JVM.set(SyncPtr(jvm)); }
            unsafe { ffi::lsplant_init(env as _) };
        }

        ipc::start_agent(pid);

        if let Some(script_list) = scripts::get_scripts_for(pkg) {
            for script in script_list {
                scripts::execute(script, pid);
            }
        }
    }

    fn pre_server_specialize(&self, _params: &mut zygisk::ServerSpecializeArgs) {}
    fn post_server_specialize(&self, _params: &zygisk::ServerSpecializeArgs) {}
}
