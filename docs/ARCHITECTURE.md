# How OneNode is built

OneNode has one protocol ([PROTOCOL.md](PROTOCOL.md)) and two implementations of it: C++/Qt for Windows, macOS and Linux,
Kotlin for Android. They do not share code; they share the **rules** and are tested against each other
(the Kotlin tests start the real desktop program).

A picture of the idea: the protocol is a **recipe**, and each implementation is a kitchen. The parts below are the stations in each kitchen.

## Desktop (C++17, Qt 6)

Everything under `src/core/` is plain Qt with no screens, so the command line tool and the tests use exactly the code the app uses.

| Part | Job |
| --- | --- |
| `Protocol` | constants, timeouts, frame reading/writing, strict JSON field readers, file-name and device-name cleaning |
| `Identity` | creates and loads this device's key and self-signed certificate; its SHA-256 fingerprint |
| `TlsConfig` | builds TLS 1.3 settings (mutual TLS, OpenSSL backend); lets any certificate through the handshake, because trust is decided later by fingerprint |
| `TrustStore` | the list of paired devices (`trusted_devices.json`), written atomically |
| `PairingEngine` | the 20-round PIN exchange as a pure state machine: no sockets, no timers, so it is testable with a fixed test vector |
| `PairingMode` | the receiver's "pair a new device" period: makes the PIN, 120 s life, one attempt, burns the PIN on failure |
| `SendSession` | one outgoing connection: hello, pairing if needed, file, checksum, result; turns errors into plain sentences |
| `ReceiveSession` | one incoming connection: the same conversation from the other side, with the timeouts and the limits of section 7 |
| `DnsMessage`, `MdnsService` | the nearby-devices list over mDNS (`_onenode._tcp`), written by hand over UDP 5353 |
| `CoreBridge` | the one object QML sees: properties and slots (receiving on/off, nearby, paired, send, PIN dialogs); owns everything above |
| `TransferModel` | the list of transfers shown on screen |

Flow of a send: *QML button -> CoreBridge::send -> SendSession -> TLS -> ReceiveSession on the other device -> file written to the receive folder*.
The screens (`qml/`) only show state and call CoreBridge; they decide nothing about security.

Rules that every part follows:

- Text that comes from the network (names, file names, error text) is cleaned before it is shown or used as a path.
- Nothing is trusted because of where it came from (IP, mDNS); only the certificate fingerprint of a paired device counts.
- A file is written to a temporary name and only renamed when the SHA-256 matches; an existing file is never replaced.

## Android (Kotlin)

Two Gradle modules.

**`:core`** is plain Kotlin (no Android classes), so it is tested on a normal computer, with the JDK's TLS and with Conscrypt (the TLS library of Android).
It is a port of the desktop rules, class for class:

| Kotlin | Desktop counterpart |
| --- | --- |
| `Protocol.kt` (`Frames`, strict readers, `Timeouts`) | `Protocol` |
| `Names.kt` | name/file-name cleaning in `Protocol` |
| `PairingEngine.kt` | `PairingEngine` (same test vector) |
| `PairingMode.kt` | `PairingMode` |
| `TrustStore.kt` | `TrustStore` |
| `Tls.kt` | `TlsConfig` + `Identity` (fingerprint, trust manager that accepts everything in the handshake) |
| `SendSession.kt`, `ReceiveSession.kt`, `ReceiveServer.kt` | `SendSession`, `ReceiveSession`, the listening server |
| `FileSink.kt`, `ProgressThrottle.kt` | where received bytes go (never overwrite; part file); progress rate limit |

**`:app`** is everything that needs Android: `AndroidIdentity` (key inside AndroidKeyStore), `Discovery` (NsdManager),
`MediaStoreSink` (saves to Downloads/OneNode), `ReceiveService` (foreground service), `Sources` (turns a picked file into something sendable),
`Engine` (the one place that owns state, like `CoreBridge`), and `MainActivity` + `ui/` (Jetpack Compose screens in the same green palette).
The screens only read `Engine`'s flows and call its functions.

## Tests

- Desktop: 12 programs in `tests/` (`ctest`): protocol parsing, sessions over real TLS on loopback, pairing engine, pairing mode, trust store, mDNS packets and a real multicast round trip.
- Android `:core`: 63 JUnit tests including real TLS on loopback, run twice (JDK TLS, Conscrypt).
- Cross-check: `InteropTest` runs the Kotlin sender against the real `onenode-cli` receiver and the other way round, including PIN pairing.
- CI runs all of it on every push (`.github/workflows/ci.yml`).
