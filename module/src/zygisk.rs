/*
 * zygisk.rs — Zygisk API v4 bindings (ZygiskNext 1.3.x / KernelSU).
 *
 * The module ABI struct passed to registerModule MUST start with:
 *   [0] api_version: i64   — ZygiskNext checks this is 1–4
 *   [1] this_object: *mut c_void
 *   [2..] function pointers for pre/post specialize callbacks
 *
 * AppSpecializeArgs fields are pointers to JNI types (jstring*, jint*, …).
 * nice_name is *mut jstring; to read it we need GetStringUTFChars + JNIEnv.
 */

use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
pub use std::sync::OnceLock;

pub type JNIEnv  = c_void;
pub type JString = *mut c_void;   /* opaque JNI local ref */

pub struct SyncPtr<T>(pub *mut T);
unsafe impl<T> Sync for SyncPtr<T> {}
unsafe impl<T> Send for SyncPtr<T> {}

pub trait ZygiskModule {
    fn pre_app_specialize(&self, _args: &mut AppSpecializeArgs) {}
    fn post_app_specialize(&self, _args: &AppSpecializeArgs, _env: *mut JNIEnv) {}
    fn pre_server_specialize(&self, _args: &mut ServerSpecializeArgs) {}
    fn post_server_specialize(&self, _args: &ServerSpecializeArgs) {}
}

/* ── AppSpecializeArgs (Zygisk API v4) ───────────────────────────────────
 * All fields are POINTERS to JNI types, matching ZygiskNext's C++ struct.
 * We only need uid and nice_name; the rest keep the layout correct.
 */
#[repr(C)]
pub struct ZygiskAppSpecializeArgs {
    pub uid:               *mut i32,
    pub gid:               *mut i32,
    pub gids:              *mut c_void,      // jintArray*
    pub runtime_flags:     *mut i32,
    pub mount_external:    *mut i32,
    pub se_info:           *mut JString,
    pub nice_name:         *mut JString,     // jstring* — use JNI to convert
    pub instruction_set:   *mut JString,
    pub app_data_dir:      *mut JString,
    pub is_child_zygote:   *mut u8,
    pub is_top_app:        *mut u8,
    pub pkg_data_info_list:         *mut c_void,
    pub whitelisted_data_info_list: *mut c_void,
    pub mount_data_dirs:   *mut u8,
    pub mount_storage_dirs: *mut u8,
}

pub struct AppSpecializeArgs<'a> {
    pub(crate) internal: &'a mut ZygiskAppSpecializeArgs,
    pub(crate) env:      *mut JNIEnv,
}

impl<'a> AppSpecializeArgs<'a> {
    /* Extract package name via JNI GetStringUTFChars.
     * Falls back to empty string if env or nice_name is null. */
    pub fn nice_name(&self) -> &str {
        unsafe {
            if self.env.is_null() || self.internal.nice_name.is_null() {
                return "";
            }
            let jstr = *self.internal.nice_name;
            if jstr.is_null() { return ""; }

            /* JNIEnv* is a pointer-to-pointer-to-vtable in C.
             * In Rust: env is *mut c_void = *mut *mut *mut FnPtr
             * GetStringUTFChars is at vtable offset 169 (JNI spec). */
            type GetStringUTFCharsFn = unsafe extern "C" fn(
                *mut c_void, *mut c_void, *mut u8) -> *const c_char;
            let vtable = *(self.env as *mut *mut *mut c_void);
            let fn_ptr = *vtable.add(169) as *const ();
            let get_utf: GetStringUTFCharsFn = std::mem::transmute(fn_ptr);
            let cstr_ptr = get_utf(self.env, jstr, std::ptr::null_mut());
            if cstr_ptr.is_null() { return ""; }
            CStr::from_ptr(cstr_ptr).to_str().unwrap_or("")
        }
    }

    pub fn set_option(&mut self, _option: ZygiskOption) { /* no-op */ }
}

pub enum ZygiskOption { DlcloseModuleLibrary }

pub struct ServerSpecializeArgs;

/* ── ZygiskApi table (first two fields used) ─────────────────────────── */
#[repr(C)]
pub struct ZygiskApi {
    pub(crate) info:            *const c_void,
    pub(crate) register_module: unsafe extern "C" fn(*mut ZygiskApi, *mut i64) -> bool,
    /* more fields follow in the real table; we don't need them */
}

/* ── Module ABI — what registerModule actually receives ─────────────── */
#[repr(C)]
pub struct ZygiskModuleAbi {
    pub api_version:          i64,         /* MUST be first; ZygiskNext checks it */
    pub this_object:          *mut c_void,
    pub pre_app_specialize:   unsafe extern "C" fn(*mut c_void, *mut ZygiskAppSpecializeArgs),
    pub post_app_specialize:  unsafe extern "C" fn(*mut c_void, *const ZygiskAppSpecializeArgs),
    pub pre_server_specialize:  unsafe extern "C" fn(*mut c_void, *mut c_void),
    pub post_server_specialize: unsafe extern "C" fn(*mut c_void, *const c_void),
}

unsafe impl Sync for ZygiskModuleAbi {}

#[macro_export]
macro_rules! register_zygisk_module {
    ($module:ty) => {
        static mut MODULE: Option<$module> = None;
        static G_JNI_ENV: $crate::zygisk::OnceLock<$crate::zygisk::SyncPtr<$crate::zygisk::JNIEnv>> =
            $crate::zygisk::OnceLock::new();

        unsafe extern "C" fn pre_app_specialize(
            _ptr: *mut std::ffi::c_void,
            args: *mut $crate::zygisk::ZygiskAppSpecializeArgs,
        ) {
            if let Some(m) = &MODULE {
                let env = G_JNI_ENV.get().map(|p| p.0).unwrap_or(std::ptr::null_mut());
                let mut wrapper = $crate::zygisk::AppSpecializeArgs {
                    internal: &mut *args, env,
                };
                m.pre_app_specialize(&mut wrapper);
            }
        }

        unsafe extern "C" fn post_app_specialize(
            _ptr: *mut std::ffi::c_void,
            args: *const $crate::zygisk::ZygiskAppSpecializeArgs,
        ) {
            if let Some(m) = &MODULE {
                let env = G_JNI_ENV.get().map(|p| p.0).unwrap_or(std::ptr::null_mut());
                let wrapper = $crate::zygisk::AppSpecializeArgs {
                    internal: &mut *(args as *mut _), env,
                };
                m.post_app_specialize(&wrapper, env);
            }
        }

        unsafe extern "C" fn pre_server_specialize(
            _ptr: *mut std::ffi::c_void,
            _args: *mut std::ffi::c_void,
        ) {
            if let Some(m) = &MODULE {
                let mut wrapper = $crate::zygisk::ServerSpecializeArgs;
                m.pre_server_specialize(&mut wrapper);
            }
        }

        unsafe extern "C" fn post_server_specialize(
            _ptr: *mut std::ffi::c_void,
            _args: *const std::ffi::c_void,
        ) {
            if let Some(m) = &MODULE {
                let wrapper = $crate::zygisk::ServerSpecializeArgs;
                m.post_server_specialize(&wrapper);
            }
        }

        static G_MODULE_ABI: $crate::zygisk::ZygiskModuleAbi = $crate::zygisk::ZygiskModuleAbi {
            api_version:           4,
            this_object:           std::ptr::null_mut(),
            pre_app_specialize,
            post_app_specialize,
            pre_server_specialize,
            post_server_specialize,
        };

        #[no_mangle]
        pub unsafe extern "C" fn zygisk_module_entry(
            api: *mut $crate::zygisk::ZygiskApi,
            env: *mut $crate::zygisk::JNIEnv,
        ) {
            let _ = G_JNI_ENV.set($crate::zygisk::SyncPtr(env));
            MODULE = Some(<$module as Default>::default());
            ((*api).register_module)(
                api,
                &G_MODULE_ABI as *const $crate::zygisk::ZygiskModuleAbi as *mut i64,
            );
        }
    };
}
