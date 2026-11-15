# Releasing OneNode

Four downloads are built by `.github/workflows/release.yml`:

| System | File | Needs |
| --- | --- | --- |
| Linux | `OneNode-<version>-linux-x86_64.AppImage` | Ubuntu 22.04 or newer (or any distribution with glibc 2.35+ and `libssl.so.3`) |
| Windows | `OneNode-<version>-windows-x64.zip` | Windows 10 or 11, 64-bit |
| macOS | `OneNode-<version>-macos-arm64.zip` | Apple silicon (M1 and newer), macOS 12+ |
| Android | `OneNode-<version>-android.apk` | Android 10 or newer |

## Try it without publishing (a dry run)

GitHub -> Actions -> **Release** -> **Run workflow**. Nothing is published. When it finishes, the four files are on the run page under *Artifacts*.

## Publish

Only when you decide:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The workflow builds everything again, checks each package, and attaches the files (and `SHA256SUMS.txt`) to a **pre-release** of that tag.
Nothing in the workflows creates a tag by itself.

## What each package check does

After packaging, the workflow starts the packaged program with `--selftest` (the command-line tool with `selftest` on Windows).
It checks that Qt's OpenSSL TLS backend loads **from inside the package** and that a device identity can be created.
On Windows the check runs with a bare `PATH`, and on macOS without `DYLD_LIBRARY_PATH`, so a missing OpenSSL file fails the build instead of reaching a user.

## First start on each system (the packages are not signed)

- **Windows:** SmartScreen says "Windows protected your PC". Choose *More info*, then *Run anyway*. (Signing needs a paid certificate.)
- **macOS:** "OneNode can't be opened because it is from an unidentified developer". Right-click the app, choose *Open*, confirm. (Notarization needs a paid Apple developer account.)
- **Linux:** `chmod +x OneNode-*.AppImage`, then run it. If it does not start, install FUSE 2 (`sudo apt install libfuse2`) or run it with `--appimage-extract-and-run`.

## Where OpenSSL comes from

OneNode always uses Qt's OpenSSL backend (the same TLS 1.3 behaviour on every system).

- **Linux:** the system's `libssl.so.3` (it receives the distribution's security updates).
- **Windows:** `libssl-3-x64.dll` and `libcrypto-3-x64.dll` from vcpkg, next to the program.
- **macOS:** `libssl.3.dylib` and `libcrypto.3.dylib` from Homebrew, inside `OneNode.app/Contents/Frameworks`. The program itself links OpenSSL statically (`-DONENODE_STATIC_OPENSSL=ON`).

To ship a security fix for OpenSSL on Windows or macOS, run the Release workflow again: it always takes the newest build of the runner.

## Android: signing the APK

An APK must be signed to install. Without any setup the workflow signs with the **debug key**: fine for trying it out, but
(1) every build made on a different machine has a different debug key, so Android refuses to update one over another
(uninstall first), and (2) it is not something to hand to other people. For a real release make **one** key and keep it forever:

```bash
keytool -genkeypair -v -keystore onenode-release.jks -alias onenode -keyalg RSA -keysize 4096 -validity 10000
base64 -w0 onenode-release.jks > onenode-release.jks.b64     # macOS: base64 -i onenode-release.jks
```

Then in GitHub -> Settings -> Secrets and variables -> Actions, add four **secrets**:

| Secret | Value |
| --- | --- |
| `ANDROID_KEYSTORE_B64` | the content of `onenode-release.jks.b64` |
| `ANDROID_KEYSTORE_PASSWORD` | the keystore password you chose |
| `ANDROID_KEY_ALIAS` | `onenode` |
| `ANDROID_KEY_PASSWORD` | the key password you chose |

**Keep a safe copy of the `.jks` file and both passwords outside GitHub.** If the key is lost, nobody can update the app any more.
The workflow verifies the signature with `apksigner verify` and prints the certificate that signed it.

Building locally: `ONENODE_KEYSTORE=/path/onenode-release.jks ONENODE_KEYSTORE_PASSWORD=... ONENODE_KEY_ALIAS=onenode ONENODE_KEY_PASSWORD=... ONENODE_VERSION_NAME=0.1.0 ONENODE_VERSION_CODE=1 ./gradlew :app:assembleRelease` inside `android/`.
`ONENODE_VERSION_CODE` must go **up** with every release (the workflow uses the run number, which always goes up).

## Android: installing

Copy the APK to the phone and open it. Android asks once to allow installs from that app (Files, Chrome, ...).
Or, with USB debugging on: `adb install -r OneNode-<version>-android.apk`.
