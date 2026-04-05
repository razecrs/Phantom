use std::fs;

pub fn get_package_name(pid: i32) -> Option<String> {
    let path = format!("/proc/{}/cmdline", pid);
    let cmdline = fs::read_to_string(path).ok()?;
    // cmdline is null-terminated, pkg is the first part
    let pkg = cmdline.split('\0').next()?.to_string();
    if pkg.is_empty() { None } else { Some(pkg) }
}

pub fn get_app_data_dir(pkg: &str) -> String {
    format!("/data/data/{}", pkg)
}
