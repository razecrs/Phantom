// @layer java
// phantom built-in: SSL unpinning (Java layer)
// Native BoringSSL tap is installed automatically by ssl_tap.c at module load.
// This script handles Java-layer pinning: OkHttp, TrustManager, conscrypt,
// Android 14+ Certificate Transparency, and network_security_config overrides.

Java.perform(() => {

  // ── OkHttp3 CertificatePinner ────────────────────────────────────────────
  try {
    const CertPinner = Java.use('okhttp3.CertificatePinner');
    CertPinner.check.overload('java.lang.String', 'java.util.List')
      .implementation = function(h) {
        ph.log(`[ssl] OkHttp3 CertificatePinner.check bypassed: ${h}`);
      };
    CertPinner.check.overload('java.lang.String', '[Ljava.security.cert.Certificate;')
      .implementation = function(h) {
        ph.log(`[ssl] OkHttp3 CertificatePinner.check(cert[]) bypassed: ${h}`);
      };
  } catch(_) {}

  // ── OkHttp3 — alternative internal builder bypass ────────────────────────
  try {
    const Builder = Java.use('okhttp3.OkHttpClient$Builder');
    Builder.certificatePinner.implementation = function(p) {
      ph.log('[ssl] OkHttpClient.Builder.certificatePinner no-op');
      return this;
    };
  } catch(_) {}

  // ── Conscrypt TrustManagerImpl ───────────────────────────────────────────
  try {
    const TMI = Java.use('com.android.org.conscrypt.TrustManagerImpl');
    TMI.verifyChain.implementation = function(chain) {
      ph.log('[ssl] conscrypt TrustManagerImpl.verifyChain bypassed');
      return chain;
    };
  } catch(_) {}

  // ── Android 14+ Certificate Transparency enforcement ─────────────────────
  // CTLogVerifier.verifySignedCertificateTimestamps throws on unknown logs.
  try {
    const CTVerifier = Java.use(
      'com.android.org.conscrypt.ct.CTLogVerifier'
    );
    CTVerifier.verifySignedCertificateTimestamps.implementation =
      function() {
        ph.log('[ssl] CTLogVerifier.verifySignedCertificateTimestamps bypassed');
        return true;
      };
  } catch(_) {}

  try {
    // Alternative class path used in some Android builds
    const CTPolicy = Java.use(
      'com.android.org.conscrypt.ct.CTEvaluator'
    );
    CTPolicy.evaluate.implementation = function(chain, scts) {
      ph.log('[ssl] CTEvaluator.evaluate bypassed');
      return Java.use(
        'com.android.org.conscrypt.ct.CTVerificationResult'
      ).$new();
    };
  } catch(_) {}

  // ── Custom X509TrustManager — accept-all shim ────────────────────────────
  try {
    const TrustManager = Java.use('javax.net.ssl.X509TrustManager');
    const SSLContext   = Java.use('javax.net.ssl.SSLContext');
    const X509Cert     = Java.use('java.security.cert.X509Certificate');

    const trustAll = Java.registerClass({
      name: 'dev.phantom.AllTrustManager',
      implements: [TrustManager],
      methods: {
        checkClientTrusted(chain, authType) {},
        checkServerTrusted(chain, authType) {
          ph.log('[ssl] AllTrustManager.checkServerTrusted — accepted');
        },
        getAcceptedIssuers() {
          return Java.array('java.security.cert.X509Certificate', []);
        },
      },
    });

    const ctx = SSLContext.getInstance('TLS');
    ctx.init(null, [trustAll.$new()], null);
    SSLContext.setDefault(ctx);
    ph.log('[ssl] SSLContext default replaced with accept-all TrustManager');
  } catch(_) {}

  // ── HttpsURLConnection hostname verifier ─────────────────────────────────
  try {
    const HTTPS = Java.use('javax.net.ssl.HttpsURLConnection');
    HTTPS.setDefaultHostnameVerifier.implementation = function() {
      ph.log('[ssl] setDefaultHostnameVerifier ignored');
    };
    HTTPS.setHostnameVerifier.implementation = function() {
      ph.log('[ssl] setHostnameVerifier ignored');
    };
  } catch(_) {}

  // ── WebViewClient SSL errors ─────────────────────────────────────────────
  try {
    const WVC = Java.use('android.webkit.WebViewClient');
    WVC.onReceivedSslError.implementation = function(view, handler) {
      ph.log('[ssl] WebViewClient.onReceivedSslError — proceeding');
      handler.proceed();
    };
  } catch(_) {}

  // ── network_security_config bypass via reflection ────────────────────────
  // Apps that pin via res/xml/network_security_config.xml use
  // NetworkSecurityTrustManager under the hood — same class as conscrypt.
  try {
    const NSTM = Java.use(
      'android.security.net.config.NetworkSecurityTrustManager'
    );
    NSTM.checkServerTrusted.implementation = function(chain, authType, host) {
      ph.log(`[ssl] NetworkSecurityTrustManager.checkServerTrusted bypassed: ${host}`);
    };
    NSTM.checkPins.implementation = function(chain) {
      ph.log('[ssl] NetworkSecurityTrustManager.checkPins bypassed');
    };
  } catch(_) {}

  // ── Retrofit / Volley share OkHttp — already covered above ───────────────

  ph.log('[ssl] Java SSL bypass active (native BoringSSL tap loaded separately)');
});
