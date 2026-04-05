# java/lib — Required JARs (not committed)

Place these here before running `java/build.sh`:

1. **rhino.jar** — Mozilla Rhino JS engine
   - Download: https://github.com/mozilla/rhino/releases
   - Use latest `rhino-X.Y.Z.jar` from the release assets
   - Tested with rhino-1.7.15

2. **android.jar** — Android SDK stubs for compilation
   - Copy from: `<android-sdk>/platforms/android-34/android.jar`
   - Used only at compile time, not bundled in DEX
