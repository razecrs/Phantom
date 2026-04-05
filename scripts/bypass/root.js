// @layer java
// phantom built-in: root detection bypass
// Covers: RootBeer, common file checks, Runtime.exec su, build props,
//         PackageManager app list, /proc/mounts, su path stat() probes.
// Native libc.access / libc.stat hooks are added by root_hide.c at module
// load — this script handles the Java layer.

Java.perform(() => {

  // ── su / Magisk / root paths to hide ─────────────────────────────────────
  const ROOT_PATHS = [
    '/su', '/su/bin', '/su/bin/su',
    '/sbin/su', '/sbin/magisk', '/sbin/.magisk',
    '/system/bin/su', '/system/xbin/su', '/system/xbin/which',
    '/system/app/Superuser.apk', '/system/app/SuperSU.apk',
    '/system/app/KingUser.apk',
    '/data/local/tmp/su', '/data/local/tmp/busybox',
    '/data/adb/magisk', '/data/adb/ksu',
    '/data/adb/modules', '/data/adb/modules_update',
    '/cache/magisk.log', '/cache/recovery',
    '/dev/com.koushikdutta.superuser.daemon/',
  ];

  // ── RootBeer ──────────────────────────────────────────────────────────────
  try {
    const RB = Java.use('com.scottyab.rootbeer.RootBeer');
    [
      'isRooted', 'isRootedWithoutBusyBox', 'detectRootManagementApps',
      'detectPotentiallyDangerousApps', 'detectTestKeys',
      'checkForBusyBoxBinary', 'checkForSuBinary',
      'checkSuExists', 'checkForRWPaths',
      'checkForDangerousProps', 'checkForRootNative',
      'detectRootCloakingApps',
    ].forEach(m => {
      try {
        RB[m].implementation = () => {
          ph.log(`[root] RootBeer.${m} → false`);
          return false;
        };
      } catch(_) {}
    });
  } catch(_) {}

  // ── java.io.File.exists — hide root paths ────────────────────────────────
  try {
    const File = Java.use('java.io.File');
    const pathSet = new Set(ROOT_PATHS);
    File.exists.implementation = function() {
      const p = this.getAbsolutePath();
      if (pathSet.has(p)) {
        ph.log(`[root] File.exists('${p}') → false`);
        return false;
      }
      return this.exists();
    };
    File.canExecute.implementation = function() {
      if (pathSet.has(this.getAbsolutePath())) return false;
      return this.canExecute();
    };
  } catch(_) {}

  // ── Runtime.exec — block su/which calls ──────────────────────────────────
  try {
    const Runtime = Java.use('java.lang.Runtime');
    const blockedCmds = ['su', 'which su', 'busybox', 'id', 'whoami'];

    function isSuspect(cmd) {
      return blockedCmds.some(b => typeof cmd === 'string' && cmd.includes(b));
    }

    Runtime.exec.overload('java.lang.String').implementation = function(cmd) {
      if (isSuspect(cmd)) {
        ph.log(`[root] Runtime.exec('${cmd}') blocked`);
        return this.exec('echo');
      }
      return this.exec(cmd);
    };
    Runtime.exec.overload('[Ljava.lang.String;').implementation = function(cmds) {
      if (cmds && cmds.length && isSuspect(cmds[0])) {
        ph.log(`[root] Runtime.exec([${cmds[0]}...]) blocked`);
        return this.exec('echo');
      }
      return this.exec(cmds);
    };
  } catch(_) {}

  // ── System.getProperty — hide test-keys build tags ───────────────────────
  try {
    const System = Java.use('java.lang.System');
    System.getProperty.overload('java.lang.String').implementation = function(k) {
      const v = this.getProperty(k);
      if (k === 'ro.build.tags' && v && v.includes('test-keys')) {
        return 'release-keys';
      }
      return v;
    };
  } catch(_) {}

  // ── PackageManager — hide root/magisk apps ───────────────────────────────
  const ROOT_PKGS = new Set([
    'com.topjohnwu.magisk', 'com.topjohnwu.magisk.alpha',
    'me.weishu.kernelsu', 'io.github.lsposed.manager',
    'org.lsposed.manager', 'com.noshufou.android.su',
    'com.koushikdutta.superuser', 'eu.chainfire.supersu',
    'com.thirdparty.superuser', 'com.yellowes.su',
    'com.ramdroid.appquarantine',
  ]);

  try {
    const PM = Java.use('android.app.ApplicationPackageManager');

    PM.getPackageInfo.overload('java.lang.String', 'int')
      .implementation = function(pkg, flags) {
        if (ROOT_PKGS.has(pkg)) {
          ph.log(`[root] getPackageInfo('${pkg}') → NameNotFoundException`);
          const exc = Java.use(
            'android.content.pm.PackageManager$NameNotFoundException'
          ).$new(pkg);
          throw exc;
        }
        return this.getPackageInfo(pkg, flags);
      };

    PM.getApplicationInfo.overload('java.lang.String', 'int')
      .implementation = function(pkg, flags) {
        if (ROOT_PKGS.has(pkg)) {
          ph.log(`[root] getApplicationInfo('${pkg}') → NameNotFoundException`);
          throw Java.use(
            'android.content.pm.PackageManager$NameNotFoundException'
          ).$new(pkg);
        }
        return this.getApplicationInfo(pkg, flags);
      };
  } catch(_) {}

  // ── /proc/mounts — strip magisk/ksu bind-mounts from output ─────────────
  // Apps read /proc/mounts to look for overlayfs / unusual loop devices.
  // We hook BufferedReader that reads from /proc/mounts and filter lines.
  try {
    const BufferedReader = Java.use('java.io.BufferedReader');
    const mountSuspect = ['/magisk', '/.magisk', '/sbin/.magisk',
                          '/data/adb', '/debug_ramdisk'];

    BufferedReader.readLine.implementation = function() {
      const line = this.readLine();
      if (line !== null) {
        for (const s of mountSuspect) {
          if (line.includes(s)) {
            ph.log(`[root] /proc/mounts line hidden: ${line}`);
            return this.readLine(); // skip — read next
          }
        }
      }
      return line;
    };
  } catch(_) {}

  ph.log('[root] Java root bypass active');
});
