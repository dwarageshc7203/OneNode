# OneNode

Send files between your own devices over the **same Wi-Fi or network**. No cloud, no account, no server of ours:
the two devices talk to each other directly, over an encrypted connection.

| | Windows | macOS | Linux | Android |
| --- | :---: | :---: | :---: | :---: |
| App | yes | yes | yes (AppImage) | yes (APK, Android 10+) |

Receiving and sending work the same way everywhere, and the apps speak one protocol ([docs/PROTOCOL.md](docs/PROTOCOL.md)), so any device can send to any other.

## How it works, in a minute

1. **Turn on Receive** on the device that should get the file. It becomes visible in the *Nearby devices* list of the others
   (or type its address by hand).
2. **First time between two devices:** on the receiver choose **Pair a new device**. It shows a 6-digit PIN. On the sender, pick the
   receiver and type the PIN. This happens **once**; both devices remember each other.
3. **Send.** Choose files, choose the device. Next time no PIN is needed.

Received files go into the receive folder (desktop: shown in Settings; Android: `Downloads/OneNode`). An existing file is never overwritten:
the new one gets a name like `photo(1).jpg`.

## Security in short

- Everything is encrypted with **TLS 1.3**, and **both** devices show a certificate (mutual TLS).
- Each device makes its own certificate on the first start. Devices are recognised by the **fingerprint** of that certificate, not by their IP address or name.
  (On Android the key lives inside the phone's Keystore and cannot be copied out.)
- A device that was never paired **cannot send a file**, whatever it announces on the network. Being visible in the nearby list proves nothing.
- Pairing uses a PIN that is **never sent**, not even hashed: the devices prove they know it bit by bit with commitments that also contain both
  fingerprints, so someone in the middle cannot relay it and cannot test guesses offline. A PIN works for **one** pairing attempt and lives 120 seconds.
- Names and file names that arrive from the network are treated as hostile text (cleaned, length-limited, no paths).

The full reasoning, including what is **not** protected, is in [docs/PROTOCOL.md](docs/PROTOCOL.md) sections 5 and 9.

## Build and test

Desktop (Qt 6, C++17, CMake, OpenSSL 3):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Android (JDK 17, Android SDK 35; Gradle wrapper included):

```bash
cd android
./gradlew :core:test :core:testConscrypt :app:testDebugUnitTest :app:assembleDebug
# the Kotlin code against the real desktop program, both directions:
ONENODE_CLI=../build/onenode-cli ./gradlew :core:test --tests '*InteropTest*'
```

More: [android/README.md](android/README.md), [docs/RELEASING.md](docs/RELEASING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## What has been verified, and what has not

Be careful with this table: "tested" means a test run on a real build, not that the code looks right.

| Area | Status |
| --- | --- |
| Desktop core (TLS, pairing, transfer, mDNS, trust list) on **Linux** | 12 test programs, all passing; two real processes talking over the network |
| Kotlin core (the same protocol for Android) | 63 tests, passing with the JDK's TLS **and** with Conscrypt (Android's TLS library); live interop with the desktop program in both directions, PIN pairing included |
| Desktop screens | rendered and clicked through on Linux; not yet seen on your Windows/macOS screens |
| **Windows and macOS** builds, packages, first start | the GitHub Actions workflows are written; **they have not run yet**. The first run may need small fixes |
| **Android app on a real phone** | builds, lints and shrinks to a 2 MB release APK; the Keystore key, the system's TLS, device discovery (NsdManager), saving through MediaStore and the receive service are **not tested on a phone yet** |
| Discovery (mDNS) between desktop and Android | the desktop side is tested against itself; Android uses the system's NsdManager, whose packets have not been checked against the desktop yet. Sending to a typed address always works as a fallback |

## Layout

```
src/core/header-files, source-files/   the desktop core: protocol, TLS, pairing, trust list, sessions, mDNS (Qt, no screens)
src/cli/main.cpp                       command line front end (send / receive / nearby / selftest)
src/main.cpp                           the desktop app (QML)
qml/                                   the desktop screens
android/                               the Android app: :core (plain Kotlin) and :app (Compose screens)
tests/                                 desktop tests
packaging/                             AppImage, Windows zip, macOS app scripts
.github/workflows/                     CI and release
docs/                                  PROTOCOL.md (the wire format), ARCHITECTURE.md, RELEASING.md, APPLYING_THE_STAGES.md
```

Third-party software and licenses: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
