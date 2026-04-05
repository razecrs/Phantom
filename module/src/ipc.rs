use std::thread;
use std::ffi::CString;
use crate::ffi;
use crate::scripts::{Script, execute};

#[cfg(unix)]
use std::os::unix::net::UnixListener;
#[cfg(unix)]
use std::io::{Read, Write};
#[cfg(unix)]
use std::fs;

const SOCK_PATH: &str = "/dev/phantom/control.sock";

/*
 * start_control_server — Unix domain socket for psh commands.
 *
 * Protocol: client sends a single UTF-8 line, server replies and closes.
 * Commands:
 *   ping                      → "pong"
 *   status                    → "Phantom attached to <pkg>"
 *   hook <script_path>        → load and execute a .js file on-the-fly
 *   dump [output_dir]         → call phantom_dex_dump, return count
 *   eval <js_source>          → evaluate one-liner JS
 *   trace <class>             → inject a trace script for the class
 */
pub fn start_control_server(pkg: &str) {
    let pkg = pkg.to_string();
    thread::spawn(move || {
        #[cfg(unix)]
        {
            let _ = fs::remove_file(SOCK_PATH);
            let listener = match UnixListener::bind(SOCK_PATH) {
                Ok(l) => l,
                Err(_) => return,
            };
            /* make socket world-writable so psh (running as shell) can connect */
            let _ = fs::set_permissions(SOCK_PATH,
                std::os::unix::fs::PermissionsExt::from_mode(0o777));

            for stream in listener.incoming() {
                let pkg2 = pkg.clone();
                match stream {
                    Ok(mut s) => {
                        let mut buf = [0u8; 2048];
                        let n = match s.read(&mut buf) {
                            Ok(n) => n,
                            Err(_) => continue,
                        };
                        let input = String::from_utf8_lossy(&buf[..n]);
                        let line  = input.trim();

                        let resp = handle_command(line, &pkg2);
                        let _ = s.write_all(resp.as_bytes());
                    }
                    Err(_) => break,
                }
            }
        }
    });
}

fn handle_command(line: &str, pkg: &str) -> String {
    let parts: Vec<&str> = line.splitn(3, ' ').collect();
    let cmd = parts[0];

    match cmd {
        "ping" => "pong\n".to_string(),

        "status" => format!("Phantom attached to {}\n", pkg),

        "hook" => {
            if parts.len() < 2 {
                return "usage: hook <script.js>\n".to_string();
            }
            let path = parts[1];
            match std::fs::read_to_string(path) {
                Ok(src) => {
                    let name = std::path::Path::new(path)
                        .file_name()
                        .and_then(|n| n.to_str())
                        .unwrap_or("hook.js")
                        .to_string();
                    let script = Script::from_source(name, src);
                    let pid = unsafe { libc::getpid() };
                    execute(script, pid);
                    format!("hooked: {}\n", path)
                }
                Err(e) => format!("error reading {}: {}\n", path, e),
            }
        }

        "eval" => {
            if parts.len() < 2 {
                return "usage: eval <js>\n".to_string();
            }
            /* reconstruct everything after "eval " */
            let src = line.trim_start_matches("eval").trim().to_string();
            let script = Script::from_source("repl.js".to_string(), src);
            let pid = unsafe { libc::getpid() };
            execute(script, pid);
            "ok\n".to_string()
        }

        "dump" => {
            let dir = if parts.len() >= 2 { parts[1] } else { "/data/phantom/dex" };
            let c_dir = match CString::new(dir) {
                Ok(s) => s,
                Err(_) => return "error: invalid path\n".to_string(),
            };
            let count = unsafe { ffi::phantom_dex_dump(c_dir.as_ptr()) };
            if count >= 0 {
                format!("dumped {} DEX file(s) to {}\n", count, dir)
            } else {
                "dump failed\n".to_string()
            }
        }

        "trace" => {
            if parts.len() < 2 {
                return "usage: trace <ClassName>\n".to_string();
            }
            let class_name = parts[1];
            /* inject a @layer java trace script */
            let src = format!(
                "// @layer java\nJava.perform(() => {{\n  \
                 // @import /data/phantom/scripts/trace/methods.js\n  \
                 trace('{}');\n}});\n",
                class_name
            );
            let script = Script::from_source("trace_inject.js".to_string(), src);
            let pid = unsafe { libc::getpid() };
            execute(script, pid);
            format!("tracing {}\n", class_name)
        }

        _ => format!("unknown command: {}\n", cmd),
    }
}

/*
 * start_agent — spun up once per process after scripts are loaded.
 * Kept minimal: its only job is to be a named thread for debugging.
 * Ring buffer I/O happens in ssl_tap hooks (native, no thread needed).
 */
pub fn start_agent(pid: i32) {
    thread::spawn(move || {
        /* nothing to do — ssl_tap hooks write to G_RB directly.
         * This thread is reserved for future async JS callbacks. */
        let _ = pid;
        loop {
            thread::sleep(std::time::Duration::from_secs(60));
        }
    });
}
