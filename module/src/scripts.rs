use std::ffi::CString;
use std::fs;
use std::path::Path;
use crate::ffi;

#[derive(Debug, Clone, PartialEq)]
pub enum Layer {
    Java,    /* @layer java   — Rhino, full JVM reflection */
    Native,  /* @layer native — QuickJS, Interceptor/Memory APIs */
    Both,    /* run in both runtimes */
}

#[derive(Debug, Clone)]
pub struct Script {
    pub name:    String,
    pub content: String,
    pub layer:   Layer,
}

impl Script {
    pub fn from_source(name: String, content: String) -> Self {
        let layer = if content.contains("@layer java") {
            Layer::Java
        } else if content.contains("@layer both") {
            Layer::Both
        } else {
            Layer::Native   /* default: QuickJS */
        };
        Script { name, content, layer }
    }
}

/* Scan a directory for .js files and return loaded Script objects. */
fn load_dir(dir: &str) -> Vec<Script> {
    let path = Path::new(dir);
    if !path.is_dir() {
        return vec![];
    }
    let mut scripts = Vec::new();
    let entries = match fs::read_dir(path) {
        Ok(e) => e,
        Err(_) => return vec![],
    };
    for entry in entries.flatten() {
        let fpath = entry.path();
        if fpath.extension().and_then(|e| e.to_str()) != Some("js") {
            continue;
        }
        let name = fpath
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("unknown.js")
            .to_string();
        let content = match fs::read_to_string(&fpath) {
            Ok(c) => c,
            Err(_) => continue,
        };
        scripts.push(Script::from_source(name, content));
    }
    /* sort by name for deterministic load order */
    scripts.sort_by(|a, b| a.name.cmp(&b.name));
    scripts
}

const SCRIPTS_BASE: &str = "/data/phantom/scripts";

pub fn has_scripts_for(pkg: &str) -> bool {
    /* check global "all" dir and per-package dir */
    let all_dir   = format!("{}/all",  SCRIPTS_BASE);
    let pkg_dir   = format!("{}/{}", SCRIPTS_BASE, pkg);
    let bypass_dir = format!("{}/bypass", SCRIPTS_BASE);

    Path::new(&all_dir).is_dir()     && fs::read_dir(&all_dir).map(|mut d| d.next().is_some()).unwrap_or(false) ||
    Path::new(&pkg_dir).is_dir()     && fs::read_dir(&pkg_dir).map(|mut d| d.next().is_some()).unwrap_or(false) ||
    Path::new(&bypass_dir).is_dir()  && fs::read_dir(&bypass_dir).map(|mut d| d.next().is_some()).unwrap_or(false)
}

pub fn get_scripts_for(pkg: &str) -> Option<Vec<Script>> {
    let mut scripts: Vec<Script> = Vec::new();

    /* 1. built-in bypass scripts (always loaded) */
    scripts.extend(load_dir(&format!("{}/bypass", SCRIPTS_BASE)));

    /* 2. global scripts (apply to all apps) */
    scripts.extend(load_dir(&format!("{}/all",    SCRIPTS_BASE)));

    /* 3. per-package scripts */
    scripts.extend(load_dir(&format!("{}/{}", SCRIPTS_BASE, pkg)));

    if scripts.is_empty() {
        None
    } else {
        Some(scripts)
    }
}

pub fn execute(script: Script, pid: i32) {
    let js   = script.content.as_str();
    let name = CString::new(script.name.as_str()).unwrap_or_default();
    let tag  = name.clone(); /* same CString for Rhino tag */

    match script.layer {
        Layer::Native => {
            exec_quickjs(js, pid, name.as_ptr());
        }
        Layer::Java => {
            exec_rhino(js, tag.as_ptr());
        }
        Layer::Both => {
            exec_quickjs(js, pid, name.as_ptr());
            exec_rhino(js, tag.as_ptr());
        }
    }
}

fn exec_quickjs(js: &str, pid: i32, name: *const std::os::raw::c_char) {
    unsafe {
        ffi::phantom_exec_quickjs(js.as_ptr() as _, js.len(), pid, name);
    }
}

fn exec_rhino(js: &str, tag: *const std::os::raw::c_char) {
    if let Some(jvm_ptr) = crate::G_JVM.get() {
        unsafe { ffi::phantom_exec_rhino(jvm_ptr.0, js.as_ptr() as _, js.len(), tag) };
    }
}
