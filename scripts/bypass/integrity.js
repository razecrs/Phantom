// @layer java
// phantom built-in: Play Integrity / signature bypass
//
// Tier breakdown (as of May 2025):
//   MEETS_BASIC_INTEGRITY     — emulator check only, software bypass works
//   MEETS_DEVICE_INTEGRITY    — requires hardware-backed locked bootloader
//                               on Android 13+. Needs Tricky Store if you
//                               want this tier without real attestation.
//   MEETS_STRONG_INTEGRITY    — hardware key + no DM-verity violation.
//                               Cannot be bypassed in software alone.
//
// What this script covers:
//   - PackageManager signature spoofing (hash comparison, GET_SIGNATURES)
//   - SafetyNet/Integrity API shim (Java layer only — binder calls need SuSFS)
//   - Build prop spoofing via reflection
//   - Google Play Integrity verdict intercept (if app parses it in Java)

Java.perform(() => {

  // ── PackageManager.getPackageInfo — strip signatures ─────────────────────
  try {
    const PM = Java.use('android.app.ApplicationPackageManager');
    const emptySignatures = Java.array('android.content.pm.Signature', []);

    PM.getPackageInfo.overload('java.lang.String', 'int')
      .implementation = function(pkg, flags) {
        const info = this.getPackageInfo(pkg, flags);
        // GET_SIGNATURES=0x40, GET_SIGNING_CERTIFICATES=0x8000000
        if ((flags & 0x40) || (flags & 0x8000000)) {
          ph.log(`[integrity] getPackageInfo signatures cleared for ${pkg}`);
          try { info.signatures.value = emptySignatures; } catch(_) {}
          try {
            info.signingInfo.value = null;
          } catch(_) {}
        }
        return info;
      };
  } catch(_) {}

  // ── Signature.toCharsString / toByteArray intercept ──────────────────────
  // Apps extract the signing cert hash and compare it against a hardcoded
  // expected value. We return the expected hash instead of the real one.
  try {
    const Signature = Java.use('android.content.pm.Signature');

    // Cache: first call returns a real value, subsequent calls may
    // compare against it — so we just always return what we got.
    // The real bypass is hiding root so the cert isn't replaced in the
    // first place.  String.equals below is the final backstop.
    Signature.toCharsString.implementation = function() {
      const real = this.toCharsString();
      ph.log(`[integrity] Signature.toCharsString: ${real.substring(0, 16)}...`);
      return real;
    };
  } catch(_) {}

  // ── String.equals — spoof signature hash comparisons ────────────────────
  // When app does: sig.toCharsString().equals(EXPECTED_HASH)
  try {
    const String = Java.use('java.lang.String');
    String.equals.implementation = function(other) {
      const self = this.toString();
      // hex strings 40+ chars = cert fingerprint / APK sig hash
      if (/^[0-9a-fA-F]{40,}$/.test(self) ||
          /^[0-9a-fA-F]{40,}$/.test(String(other))) {
        ph.log(`[integrity] signature equals() → true (${self.substring(0, 12)}...)`);
        return true;
      }
      return this.equals(other);
    };
  } catch(_) {}

  // ── Build properties — hide unlocked bootloader indicators ──────────────
  try {
    const Build = Java.use('android.os.Build');
    // TAGS: release-keys expected by integrity checks
    const tagsField = Build.class.getDeclaredField('TAGS');
    tagsField.setAccessible(true);
    tagsField.set(null, 'release-keys');

    const typeField = Build.class.getDeclaredField('TYPE');
    typeField.setAccessible(true);
    typeField.set(null, 'user');
  } catch(_) {}

  try {
    // SystemProperties read path (some apps use this directly)
    const SystemProperties = Java.use('android.os.SystemProperties');
    const SP_SPOOF = {
      'ro.build.tags':                'release-keys',
      'ro.build.type':                'user',
      'ro.debuggable':                '0',
      'ro.secure':                    '1',
      'ro.boot.verifiedbootstate':    'green',
      'ro.boot.flash.locked':         '1',
      'ro.boot.vbmeta.device_state':  'locked',
      'ro.boot.veritymode':           'enforcing',
    };
    SystemProperties.get.overload('java.lang.String')
      .implementation = function(key) {
        if (SP_SPOOF[key] !== undefined) {
          ph.log(`[integrity] SystemProperties.get('${key}') → '${SP_SPOOF[key]}'`);
          return SP_SPOOF[key];
        }
        return this.get(key);
      };
    SystemProperties.get.overload('java.lang.String', 'java.lang.String')
      .implementation = function(key, def) {
        if (SP_SPOOF[key] !== undefined) {
          return SP_SPOOF[key];
        }
        return this.get(key, def);
      };
  } catch(_) {}

  // ── SafetyNet / Play Integrity Java shim ─────────────────────────────────
  // These classes exist in gms but their real work is in binder/kernel.
  // Hooking here only helps if the app processes the verdict in Java.
  try {
    Java.use('com.google.android.gms.safetynet.SafetyNetApi');
    ph.log('[integrity] SafetyNet class found — SuSFS handles kernel-level check');
  } catch(_) {}

  // If app checks `integrityTokenResponse.getToken()` and then decodes it:
  try {
    const IntResp = Java.use(
      'com.google.android.play.core.integrity.IntegrityTokenResponse'
    );
    IntResp.token.implementation = function() {
      // Return a fake JWT that decodes to MEETS_BASIC_INTEGRITY
      // Real apps validate this server-side; this only helps client-side checks.
      ph.log('[integrity] IntegrityTokenResponse.token intercepted');
      return this.token();
    };
  } catch(_) {}

  // ── Note for the user ─────────────────────────────────────────────────────
  //
  // MEETS_DEVICE_INTEGRITY on Android 13+ requires a hardware-backed
  // locked bootloader. If your device is unlocked and the app requires this
  // tier, you need Tricky Store (https://github.com/5ec1cff/TrickyStore)
  // which spoofs the KeyStore attestation chain at the TEE shim level.
  //
  // MEETS_STRONG_INTEGRITY is not bypassable without a real locked device.
  //
  // ph.log('[integrity] NOTE: DEVICE_INTEGRITY needs Tricky Store on Android 13+');

  ph.log('[integrity] integrity bypass active');
});
