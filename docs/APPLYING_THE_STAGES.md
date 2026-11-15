# Applying the stages, in order

The work was delivered in five ZIPs. Each one is a set of files to copy over the repository (same folders). **Apply them in this order;
a later ZIP may overwrite a file of an earlier one, and that is intended** (for example `corebridge.cpp` grows in every desktop stage).

| # | ZIP | What it adds |
| --- | --- | --- |
| 1 | `onenode-stage1-security-core.zip` | PIN pairing engine, pairing mode, send/receive sessions v2, CoreBridge, protocol v2 text |
| 2 | `onenode-stage2-mdns.zip` | mDNS/DNS-SD: the nearby list, `DnsMessage`, `MdnsService` |
| 3 | `onenode-stage3-gui.zip` | the desktop screens (QML) for pairing, nearby devices, settings |
| 4 | `onenode-stage4-ci-release.zip` | workflows, AppImage/Windows/macOS packaging, `--selftest` |
| 5 | `onenode-stage5-android-docs.zip` | the Android app, workflows with Android, README, ARCHITECTURE, this guide |

After copying, do the CMake edits below (the ZIPs contain no `CMakeLists.txt` of yours: it is yours and may differ from mine).
`docs/reference/CMakeLists.core.reference.txt` shows the library, CLI and tests part to **compare against**. It leaves out your GUI target on purpose: that part is yours.

## CMake edits

**Top-level `CMakeLists.txt`**

1. In `add_library(onenode_core STATIC ...)` list every `.cpp` and `.h` of `src/core/` (the reference shows the full list). New since your first version:
   `dnsmessage`, `mdnsservice`, `pairingengine`, `pairingmode`, `tlsconfig`, `truststore`, `identity`, `progressthrottle.h`, `netutil.h`.
2. `target_link_libraries(onenode_core PUBLIC Qt6::Network OpenSSL::Crypto)` and `find_package(OpenSSL 3.0 REQUIRED)`.
3. Above `find_package(OpenSSL ...)`, the static-OpenSSL option (macOS release build): see `docs/CMAKE_CHANGES.md`.
4. In `qt_add_qml_module(appOneNode ...)` add the four new QML files to `QML_FILES`: `qml/PinDialog.qml`, `qml/PairDialog.qml`, `qml/NearbyRow.qml`, `qml/PairedRow.qml`.
5. `enable_testing()` and `add_subdirectory(tests)`.

**`tests/CMakeLists.txt`**: one block per test program; the reference shows all twelve
(`test_protocol test_session test_corebridge test_transfermodel test_identity test_tls test_truststore test_pairingengine test_pairingmode test_dnsmessage test_mdns test_mdns_udp`).

## Check each stage

```bash
cmake -S . -B build/Desktop-Debug
cmake --build build/Desktop-Debug
QT_QPA_PLATFORM=offscreen ctest --test-dir build/Desktop-Debug --output-on-failure
```

Expected after stage 2 and later: **12 of 12 tests passed**. (`test_mdns_udp` needs working multicast on your machine; on a build server it can skip itself.)

## Android

Unzip stage 5, then:

```bash
cd android
./gradlew :core:test :core:testConscrypt :app:assembleDebug
```

If `gradlew` is not executable after unzipping: `chmod +x android/gradlew` and, for git, `git update-index --chmod=+x android/gradlew`.
Android Studio: *Open* the `android` folder (not the repository root).

## Git

Nothing here creates a tag. Releasing is your decision: see `docs/RELEASING.md`.
Add these lines to the repository's `.gitignore` if they are not there yet: `.gradle/`, `local.properties`, `*.jks`, `*.keystore` (the `android/.gitignore` already has them).
