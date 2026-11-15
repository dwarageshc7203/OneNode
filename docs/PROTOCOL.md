# OneNode Protocol v2

Version 2 adds **encryption (TLS)** and **pairing with a PIN** (a one-time introduction between two devices).
Version 1 sent everything in plain text; it is retired and is not accepted any more.

Words used below:

- **sender**: the device that connects and sends a file. **receiver**: the device that listens.
- **fingerprint**: the SHA-256 of a device's certificate (its "ID card"), 32 bytes, written as 64 lowercase hex characters.
- **trusted**: the device has the other one's fingerprint saved in its trusted-devices list.
- **pairing mode**: a short period, started by the receiver's user, in which the receiver shows a PIN and accepts one new device.
- MUST / SHOULD have their usual meaning: MUST = required, SHOULD = recommended.

---

## 1. Transport: TLS

- Everything runs over **one TCP port** (default `45454`), inside **TLS 1.3**. There is no plain-text mode and no fallback.
- Implementations on all platforms use the same TLS stack behaviour: the desktop app uses OpenSSL (macOS's and Windows' built-in TLS are not used, because TLS 1.3 with client certificates is not reliable there), the Android app uses the system's TLS 1.3 (Conscrypt).
- **Both sides present a certificate** (mutual TLS). The receiver MUST ask the sender for one.
- Every device owns one **self-signed certificate** and its private key, created on first start and kept for good.
- **Trust is decided by fingerprint, not by certificate chains.** Expiry dates, issuer and subject name are ignored.
  A handshake with an unknown fingerprint is allowed (it is needed for pairing), but an unknown device may only
  do `hello` and pairing, never send a file.
- After the handshake each side reads the other's fingerprint from its certificate. Nobody ever *tells* a fingerprint in a message.
- Because trust follows the fingerprint, **how a device was found does not matter** (typed address, later also mDNS):
  a trusted device never needs the PIN again, and an announcement on the network never makes a device trusted.

## 2. Framing (unchanged from v1)

```
[ 4 bytes: N, unsigned, big endian ][ N bytes: UTF-8 JSON header ]  [ body, only after a "file" ]  [ file_end frame ]
```

- `N` is the JSON length in bytes, `1 <= N <= 16384` (16 KiB). Anything else is invalid: close the connection.
- Every JSON object has `"v"` (integer, `2`) and `"type"` (string).
- A message that is not valid JSON, has no `v`/`type`, or has fields of the wrong type: reply `invalid_header` and close.
- Unknown `type` or `v` other than 2: reply `unsupported` and close.
- Unknown extra fields are ignored.

## 3. Messages

| type | from | fields |
|---|---|---|
| `hello` | sender | `name` (string), `known` (boolean: "this device has the receiver's fingerprint on its list") |
| `hello_result` | receiver | `status`: `"ok"` or `"pairing_required"`, or `"error"` + `code`; `name` (string, the receiver's own display name) |
| `pair_commit` | both | `round` (integer 0 to 19), `commit` (string, 64 lowercase hex) |
| `pair_open` | both | `round` (integer 0 to 19), `nonce` (string, 32 lowercase hex) |
| `pair_result` | receiver | `status`: `"ok"` or `"error"` + `code` |
| `file` | sender | `filename` (string, not empty), `size` (integer >= 0) |
| `file_end` | sender | `sha256` (string, 64 lowercase hex of the body bytes; sent for every file, also empty ones) |
| `file_result` | receiver | `status`: `"ok"` or `"error"` + `code` |

`name` is the device's display name. It comes from the other side, so it is **untrusted text**: a receiver MUST
strip control characters, limit it to 64 characters, and treat an empty name as `"Unknown device"` before showing it.

## 4. Conversation

```
sender                                              receiver
  | -------- TLS 1.3 handshake, both certificates -------- |
  | -- hello {name, known} ---------------------------->    |   receiver looks up the sender's fingerprint
  | <-- hello_result {status, name} -------------------    |
  |                                                        |
  |   both sides trust each other: skip to "file"          |
  |   otherwise: the user types the PIN, then 20 rounds:   |
  |                                                        |
  | -- pair_commit {round i} ------------------------->    |   round i (i = 0 ... 19)
  | <-- pair_commit {round i} -------------------------    |
  | -- pair_open {round i} --------------------------->    |
  | <-- pair_open {round i} ---------------------------    |
  |        ... 20 rounds in total ...                      |
  | <-- pair_result {status} --------------------------    |
  |                                                        |
  | -- file {filename, size} ------------------------->    |
  | -- body bytes ------------------------------------>    |
  | -- file_end {sha256} ----------------------------->    |
  | <-- file_result {status[, code]} ------------------    |
```

One connection carries one file (as in v1). The receiver closes the connection after `file_result`.

### 4.1 What the receiver answers to `hello`

The receiver looks up the sender's fingerprint (from the TLS handshake) and reads `known` from `hello`.

| sender's fingerprint is on the receiver's list | `known` | receiver is in pairing mode | answer |
|---|---|---|---|
| yes | true | (any) | `ok` |
| yes | false | yes, free | `pairing_required` |
| no | (any) | yes, free | `pairing_required` |
| any other combination, pairing mode off | | no | `error` with `pairing_off` (and nothing is shown on screen) |
| | | yes, but another sender is already pairing | `error` with `busy` |

`known: false` from a device the receiver does trust means "I have forgotten you" (for example the sender removed the receiver from its list).
A plain `ok` would then be unsafe for the sender, so both sides pair again.

### 4.2 What the sender does after `hello_result`

| receiver says | the sender... |
|---|---|
| `ok` | checks that the receiver's fingerprint IS on its own list, then sends `file`. If it is not (a misbehaving receiver), it stops: it never sends a file to a device it does not know. |
| `pairing_required` | asks the user for the PIN shown on the receiver and starts pairing |
| `error` | shows the code as a sentence and stops |

A file is only sent when **both** sides know each other. While the user types the PIN the connection stays open (the receiver waits up to 60 seconds for the first pairing message).
A sender that has several files for one receiver sends the first one alone and starts the others only after `ok` or a finished pairing, so the user types the PIN once.

### 4.3 More rules for the receiver

- `file` from a sender that is **not trusted**: reply `not_paired` and close. This holds at any moment.
- Pairing never starts by itself: a receiver that is not in pairing mode answers `pairing_off` to every unknown device
  and shows **nothing** to its user. A stranger cannot make a prompt appear.

## 5. Pairing

Goal: two devices that have never met introduce themselves safely. The receiver's user proves presence in front of the
receiver by reading a PIN off its screen; the sender's user types it in.

### 5.1 Pairing mode (receiver)

- Only the receiver's own user can start it (a button such as "Pair a new device"). Nothing from the network can.
- It creates a **PIN**: a number from 0 to 999 999 chosen uniformly at random with a cryptographically secure random source,
  shown as 6 digits with leading zeros, grouped "482 913". Starting it again creates a **new** PIN.
- It lasts **120 seconds**, then the PIN is thrown away.
- **A PIN is single use.** If an exchange *begins* (the first `pair_commit` arrived) and does not end in success, for any reason (a wrong PIN, a failed check, a timeout, the connection dropping), the PIN is burnt and
  the mode ends. The user starts it again to get a new PIN. (Section 5.2 explains why this matters.)
  A sender that connects, gets `pairing_required` and leaves *before* sending the first `pair_commit` does not burn the PIN.
- It allows **one sender at a time**: from `pairing_required` until the sender leaves or the exchange ends, any other `hello` that needs pairing gets `busy`.
- On success the mode ends (one PIN pairs one device).

### 5.2 Why the PIN is never sent, not even hashed

A PIN has only 1 000 000 possible values. Anything computed from it and sent over the network (a hash, a proof, a signature)
can be tried against all 1 000 000 values in about a second. Someone sitting between the two devices would collect that value and
recover the PIN offline, then pair as the sender. A slow hash only raises the price. So the PIN is **never** put on the wire in any form
that can be tested offline. Instead the devices reveal it **one bit at a time** with commitments, as in the passkey entry of Bluetooth LE
(only the recipe is borrowed, no Bluetooth is involved), and each commitment also contains **both fingerprints**.

**Why a PIN may only be used once.** In every round the sender reveals its nonce (and so, to someone who checks, its bit) *before* it has verified the receiver's commitment.
A fake receiver can therefore learn one real bit of the PIN per round from an honest sender, even though the exchange then fails.
If the same PIN could be tried again, those bits would add up and the attacker's odds would collapse. With a single-use PIN the learned bits are worthless, because the next attempt uses a new PIN.
The receiver never reveals anything before it has verified the other side, so a fake sender learns nothing from the receiver.

### 5.3 The exchange

The PIN is a 20-bit number (2^20 = 1 048 576 > 1 000 000). Bit `i` of the PIN (`i` = 0 to 19, most significant first):

```
bit(i) = (PIN >> (19 - i)) AND 1                         (a value 0 or 1)
```

Both sides know the PIN (the receiver made it, the sender's user typed it). Both know both fingerprints from the TLS handshake:
`fpS` = fingerprint of the sender, `fpR` = fingerprint of the receiver, each as 32 raw bytes.

Every round `i` has four messages, always in this order:

1. sender → receiver `pair_commit` {round: i, commit: `commitS`}
2. receiver → sender `pair_commit` {round: i, commit: `commitR`}   (only after step 1)
3. sender → receiver `pair_open` {round: i, nonce: `nonceS`}       (only after step 2)
4. receiver → sender `pair_open` {round: i, nonce: `nonceR`}       (only after step 3)

The nonces are fresh 16-byte values from a cryptographically secure random source for each round. The commit is

```
commit = SHA-256( "OneNode-pair-v2"  ||  role  ||  fpS  ||  fpR  ||  i (1 byte)  ||  nonce (16 bytes)  ||  bit (1 byte) )
role   = the single byte 0x53 ("S") for the sender, 0x52 ("R") for the receiver
```

Checks, which every side does for every round, using **its own** copy of the PIN and **its own** view of the fingerprints:

- After step 3 the receiver recomputes `commitS` from `nonceS` and *its own* `bit(i)`. It must equal the commit received in step 1.
- After step 4 the sender recomputes `commitR` from `nonceR` and *its own* `bit(i)`. It must equal the commit received in step 2.

If a check fails, or a message arrives out of order, with a wrong round number, or too late: the side that notices closes the connection
(the receiver first sends `pair_result` with `pairing_failed`). The receiver counts one failed exchange (5.1). Round `i + 1` only starts after round `i`
passed. A wrong PIN is caught in the first round in which the bits differ, and two devices with different PINs never get through all 20 rounds.

After round 19 the receiver saves the sender (fingerprint + name from `hello`) as trusted and sends `pair_result` with `ok`.
On `ok` the sender saves the receiver (fingerprint + name from `hello_result`) as trusted and carries on with `file`.
If the connection breaks right after the receiver saved its side, the receiver trusts a sender that does not trust it yet; the next send simply pairs again (4.2).

### 5.4 Why this holds

- **The order is the point.** The sender commits first, then the receiver commits, and only then does the sender open. Someone in the middle
  has to commit to a bit toward one device before it learns anything about that bit, so each round it can only guess: it is right half of the time.
  All 20 rounds: 1 chance in 1 048 576 per PIN, and a PIN is used for one exchange only.
- **A commitment cannot be changed afterwards** and it contains both fingerprints. A device in the middle that shows each side its own fake certificate
  makes the two sides compute different commitments, so it fails in round 0 even if the correct PIN was typed. It cannot simply relay.
- **Nothing is testable offline.** The nonces are random and fresh each round, so a recording of an exchange gives no way to try PINs.
- A reference model of this exchange was run: honest devices with the same PIN pair; with a wrong PIN none of 20 000 pairs; a device in the middle
  with its own certificates fails in round 0; and a guessing middleman's success measured 0.0157, 0.00385 and 0.00095 for 6, 8 and 10 rounds,
  against the theoretical 2^-6, 2^-8 and 2^-10.

### 5.5 Test vector

The following must be reproduced by an implementation. `fpS` = byte `0x11` repeated 32 times, `fpR` = byte `0x22` repeated 32 times, PIN = `482913`
(binary over 20 bits: `01110101111001100001`). The nonces of round `i` are fixed for this vector: `nonceS` = byte `i` repeated 16 times,
`nonceR` = byte `0x80 + i` repeated 16 times.

| round | bit | nonceS | commitS |
|---|---|---|---|
| 0 | 0 | `00000000000000000000000000000000` | `04c1fd647242b4d7369c627a7f2e1931432e9a8dcd1d4012f0b70aefed833bf0` |
| 1 | 1 | `01010101010101010101010101010101` | `9e00bdc6f21e1212c88a9ef586e27c08e2e31985b9a7e5a2e71a8ee995fca376` |
| 19 | 1 | `13131313131313131313131313131313` | `4554b6c5592979e5e9ab1678ceb60064c945f2f2d9ade66b981a57f03861114c` |

| round | bit | nonceR | commitR |
|---|---|---|---|
| 0 | 0 | `80808080808080808080808080808080` | `cdd47beb0cabc581177bae31264da90c001191aff0bf363b73e6fe5e38a9366a` |
| 1 | 1 | `81818181818181818181818181818181` | `e0638040a1d70cb798fe3d2e6080e154d38f9a289483a3d7c4179554167e6895` |
| 19 | 1 | `93939393939393939393939393939393` | `1d79bf59f8f49c6f264523393b0d7c10aa05776480970afdee29dc3b05d018cd` |

## 6. Errors

`code` values in `status: "error"` replies:

| code | meaning |
|---|---|
| `invalid_header` | bad frame, bad JSON, missing/wrong fields (also a bad `file_end`) |
| `invalid_filename` | empty name, or a name that is not a plain file name |
| `unsupported` | unknown `type`, or `v` is not 2 |
| `write_failed` | the receiver could not write the file |
| `checksum_mismatch` | the received bytes do not match `file_end.sha256` |
| `not_paired` | a file was offered by a device that is not trusted |
| `pairing_off` | the receiver is not in pairing mode |
| `busy` | another pairing exchange is running |
| `pairing_failed` | a round check failed, or the steps came in a wrong order |
| `pairing_timeout` | the PIN's 120 seconds ended |
| `timeout` | the other device took too long (see section 7) |

Rules: every error path sends **exactly one** reply and then closes. `file_result` `ok` is sent only after the file is fully
written, its checksum verified, and its final name in place. Existing files are never overwritten.

## 7. Limits and timeouts

| what | limit |
|---|---|
| JSON header | 1 to 16384 bytes |
| TLS handshake | 10 s |
| first message (`hello`) after the handshake | 10 s |
| PIN lifetime (pairing mode) | 120 s |
| PIN used for | one exchange |
| after `pairing_required`: the first `pair_commit` | 60 s (the user is typing) |
| each pairing message, once round 0 has started | 5 s |
| the whole exchange (20 rounds) | 30 s |
| nothing received for | 30 s while a file is moving (the body counts as stalled; the receiver deletes the partial file) |
| device name | 64 characters, after cleaning |

When a limit runs out, the side that notices sends one error reply with `timeout` (where a reply is still possible) and closes the connection.

## 8. Finding devices: mDNS / DNS-SD (the "nearby devices" list)

Typing an IP address works always. On top of that, devices announce themselves on the local network so that the
other devices can show them in a list. This is standard DNS-SD over multicast DNS (RFC 6762 and RFC 6763), so Android's
`NsdManager` and Apple's Bonjour can take part without any special code.

**Nothing learned this way is trusted.** Anybody on the network can announce anything. The list only saves typing.
Trust still comes from pairing and from the TLS fingerprint, exactly as in sections 4 and 5.

### 8.1 What is announced

| Item | Value |
| --- | --- |
| Multicast group and port | `224.0.0.251`, UDP `5353` (IPv4 only for now) |
| Service type | `_onenode._tcp.local` |
| Instance name | `OneNode-<first 8 hex of the fingerprint>._onenode._tcp.local` |
| Host name | `onenode-<first 8 hex of the fingerprint>.local` |
| `PTR` | `_onenode._tcp.local` -> instance name |
| `SRV` | instance -> host name and the TCP port of the TLS server |
| `A` | host name -> the IPv4 address of that network connection |
| `TXT` | `v=2`, `fp=<64 lowercase hex>`, `name=<device name>`, and `pair=1` only while "Pair a new device" mode is open |
| TTL | 120 seconds for every record; `0` means "goodbye" |

- `v` is the protocol version. A device with another `v` is not listed (the transfer would be refused anyway).
- `fp` is what the device *claims*. The certificate in the TLS handshake is the truth: a sender that picked a device from
  the list passes the claimed fingerprint along, and **stops before sending anything** when the certificate differs.
- `name` is cleaned like every other name (section 5.1) and is cut so that the whole `name=...` entry stays under 255 bytes.
- `pair=1` lets the screen show which devices are waiting for a new device. It is only a hint; the receiver still decides.

### 8.2 When packets are sent

- On start: the announcement (all four records in the answer section) and a query for `_onenode._tcp.local` `PTR`;
  the announcement is repeated one second later.
- The query is repeated after 1, 2, 4, 8 ... up to 60 seconds, then every 60 seconds.
- The announcement is repeated every 60 seconds (the records live for 120).
- When the name or the pairing mode changes: a new announcement at once.
- On a clean stop: the same records with TTL 0 (goodbye).
- A query for the service, the instance or the host name is answered with the full announcement, at most once per second
  per network connection.

### 8.3 What a receiver of these packets does

Everything from the network is hostile until proven otherwise.

- A packet is rejected when it is longer than 9000 bytes, malformed, uses a compression pointer that does not point
  backwards, loops, claims more records than it can hold, or uses a name longer than 255 bytes. Rejecting never crashes and never allocates a lot.
- Packets from outside the local network (source address not in the connection's subnet) and packets whose source port is not 5353 are ignored.
- At most 500 packets are looked at per second.
- A record is used only when its name ends with `._onenode._tcp.local`, the `TXT` has `v=2` and a valid, lowercase `fp`,
  the `SRV` port is not 0, and the address is a real IPv4 address (not loopback, any, broadcast or multicast).
- A device with no `A` record is reached at the address its packet came from.
- At most 64 devices are remembered; a flood of fake devices cannot grow memory or push real devices out.
- Records we sent ourselves (we hear our own multicast) and records that use our own fingerprint are ignored.
- A device disappears when its `SRV` or `TXT` expires, or when its goodbye (TTL 0) arrives.

Not done on purpose: IPv6, conflict probing (two devices with the same first 8 hex digits would have to start at the same
moment, and the full fingerprint is checked anyway), known-answer suppression, per-interface host names.

## 9. What this protects, and what it does not

Protected:

- Someone listening on the Wi-Fi cannot read the files.
- A device that has never been paired cannot put a file on a receiver.
- A device that cannot see the receiver's screen cannot pair: it does not know the PIN and its blind chance is 1 in 1 048 576 per PIN (and a PIN gets one exchange).
- A device in the middle during pairing succeeds with the same tiny chance, whether or not the user is careful.
- A paired device is recognised by its certificate, not by its IP address, so a changing IP does not matter.

Not protected:

- Someone who can see the receiver's screen while the PIN is shown can read the PIN.
- Someone on the network can spoil pairing (denial of service): one failed exchange burns the PIN, and an idle connection can hold the single pairing place for up to 60 seconds. They still cannot pair; the user starts it again.
- A trusted device that is hacked can still send files to you. They only ever land in the receive folder under a new name, never over an existing file.
- Someone can still see *that* two devices talk, and how big the transfers are.
- Anybody on the network can announce a fake device in the nearby list, or hide a real one (denial of service). A fake entry cannot receive anything useful: it has no certificate that matches a paired device, and it does not know the PIN.
- Anyone who can copy a device's private key file can pretend to be that device. The key file MUST be readable by the owner only.

## 10. Local storage (not part of the wire protocol; for implementers)

In the app's data folder:

- `identity.key` and `identity.crt` (PEM): the device's key and certificate. The key file is owner-only (mode 0600 on Linux).
- `trusted_devices.json`: `{ "schemaVersion": 1, "devices": [ { "fingerprint": "<64 hex>", "name": "...", "pairedAt": "<ISO time>" } ] }`.
  It is written by writing a temporary file and renaming it, so a crash never leaves a half-written list.
- A trusted device can be removed from the list at any time; it then has to pair again.
- The PIN is never saved anywhere.

Key type: **ECDSA P-256** on every platform (self-signed certificate). Wire-wise only the fingerprint matters, so another
implementation may choose differently, but it MUST be able to do TLS 1.3 with a client certificate.

## 11. Android implementation notes (not part of the wire protocol)

The Android app follows sections 1 to 10 exactly. How it gets there on Android:

- **TLS:** the platform's own TLS (Conscrypt), TLS 1.3 only, with client certificates required on the receiving side.
  A custom trust manager lets every certificate through the *handshake*; the app then decides by fingerprint (section 1).
  A custom key manager always offers the one device certificate.
- **Key:** an EC secp256r1 key generated **inside AndroidKeyStore** (it cannot be exported). The Keystore makes the
  self-signed certificate (30 years). Section 9's "key file readable by the owner only" is met by the key never being a file.
- **Trusted devices:** `trusted_devices.json` in the app's private storage (section 10 schema), written atomically.
  `allowBackup` is off and the data-extraction rules exclude everything, so neither list nor key travels in a backup.
- **Finding devices:** `NsdManager` registers and browses `_onenode._tcp` with the TXT keys of section 8 (`v`, `fp`, `name`, `pair`).
  The service is registered again when pairing mode turns on/off or the device is renamed. At most 64 peers are kept; IPv4 is preferred.
  Android's NSD does not support everything in section 8 (for example the exact TTL handling); typing an address always works.
- **Saving files:** MediaStore, `Downloads/OneNode`, no storage permission. The file is created as *pending* and only becomes
  visible after the SHA-256 of section 4 is verified; a failed or cancelled transfer leaves nothing behind. A name that already exists gets `(1)`, `(2)`, ...
- **Receiving in the background:** a foreground service (type `dataSync`) with a notification, started when the user turns Receive on.
  Android 15 limits such services to about six hours per day; the service stops itself when the system says so.
  A wake lock is held only while a transfer runs.
- **Minimum:** Android 10 (API 29); built for API 35.
