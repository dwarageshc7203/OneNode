# Third-party software in OneNode

OneNode uses the following software. Each keeps its own license.

## Qt 6 (QtCore, QtGui, QtNetwork, QtQml, QtQuick, QtQuick Controls, QtQuick Dialogs)
Licensed under the GNU Lesser General Public License v3 (LGPLv3). OneNode uses Qt as shared libraries (DLLs, frameworks,
`.so` files) that are shipped next to the program, so they can be replaced by another build of the same Qt version.
License text and source code: <https://www.qt.io/licensing/> and <https://code.qt.io/>.

## OpenSSL 3
Licensed under the Apache License 2.0. Used for TLS 1.3 and for creating device certificates.
<https://www.openssl.org/> , license: <https://www.openssl.org/source/license.html>

## Inter (font)
Copyright (c) 2016 The Inter Project Authors. Licensed under the SIL Open Font License 1.1.
The full text is in `qml/fonts/OFL.txt` (and in the source repository next to the font files).
<https://github.com/rsms/inter>

## Android app
The Android app is built from the following. Each keeps its own license.

- **Kotlin** and its standard library, **AndroidX** (Core, Activity, Compose, Material 3, Lifecycle): Apache License 2.0.
  <https://kotlinlang.org/>, <https://developer.android.com/jetpack>
- TLS on Android is the platform's own (Conscrypt, part of Android, Apache License 2.0).

Used only to run the automated tests, not shipped in the app: JUnit 4 (Eclipse Public License 1.0), `org.json` (JSON License / public-domain
reference implementation), Conscrypt desktop build (Apache License 2.0).
