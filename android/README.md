# OneNode for Android

Kotlin, Jetpack Compose, minimum Android 10 (API 29), target 35. Two modules: `:core` (the protocol, plain Kotlin) and `:app` (the screens and Android parts).
How it fits with the desktop apps: [../docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md). The wire format: [../docs/PROTOCOL.md](../docs/PROTOCOL.md) (section 11 = Android notes).

## Build and test

Needs JDK 17 (21 also works) and the Android SDK with platform 35 and build-tools 35.0.0.
Put `sdk.dir=/path/to/Android/Sdk` in `android/local.properties` (Android Studio does this for you; the file is git-ignored).

```bash
./gradlew :core:test :core:testConscrypt     # protocol tests, with the JDK's TLS and with Conscrypt
./gradlew :app:testDebugUnitTest :app:lintDebug
./gradlew :app:assembleDebug                 # app/build/outputs/apk/debug/app-debug.apk
./gradlew :app:assembleRelease               # shrunk release APK (about 2 MB); signing: see ../docs/RELEASING.md

# against the real desktop program (build it first: cmake --build build --target onenode-cli)
ONENODE_CLI=$PWD/../build/onenode-cli ./gradlew :core:test --tests '*InteropTest*' --rerun-tasks
```

Android Studio: *Open* this `android` folder.
Install on a phone with USB debugging: `adb install -r app/build/outputs/apk/debug/app-debug.apk`.

## Trying it

1. On the phone, turn **Receive** on (a notification shows while it listens).
2. On the desktop, run OneNode; the phone appears under *Nearby devices* (if it does not, send to its address by hand: the phone does not display its own address, look under the phone's Wi-Fi network details).
3. First time: on the receiving device choose **Pair a new device** (the *Pair* button), read the PIN, and type it on the sending device.
4. Files received on the phone are in **Downloads/OneNode**. From other apps use *Share -> OneNode*.

## Things to know

- Phone and computer must be on the same network; some guest Wi-Fi networks block device-to-device traffic.
- Android may stop the receiving service after about six hours (a system rule since Android 15); turn Receive on again.
- A debug or unsigned-secret build is signed with a throwaway key; see RELEASING.md for the real key.
- Not yet tried on a real phone: the Keystore key, device discovery and the receiving service. See the table in the main README.
