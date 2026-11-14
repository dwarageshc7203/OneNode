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
| `hello` | sender | `name` (string) |
| `hello_result` | receiver | `status`: `"ok"` or `"pairing_required"`, or `"error"` + `code`; `name` (string) |
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
  | -- hello {name} ---------------------------------->    |   receiver looks up the sender's fingerprint
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

| the sender's fingerprint is | receiver is in pairing mode | answer |
|---|---|---|
| trusted | (any) | `ok` |
| not trusted | yes | `pairing_required` |
| not trusted | no | `error` with `pairing_off` (and nothing is shown on screen) |

### 4.2 What the sender does after `hello_result`

| receiver says | sender trusts the receiver's fingerprint | the sender... |
|---|---|---|
| `ok` | yes | sends `file` |
| `ok` | no | asks the user for the PIN and starts pairing |
| `pairing_required` | yes or no | asks the user for the PIN and starts pairing |

So a file is only sent when **both** sides know each other. In every other case the two devices introduce themselves again.
While the user types the PIN the connection stays open; it is closed when the receiver's pairing mode ends.

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
  shown as 6 digits with leading zeros, grouped "482 913".
- It lasts **120 seconds**, then the PIN is thrown away.
- It allows **3 failed exchanges**. After the third the mode ends and the PIN is thrown away. A new mode creates a new PIN.
- It allows **one exchange at a time**: a `pair_commit` for round 0 while another exchange is running gets `pair_result`
  with `busy`, and the connection is closed.
- On success the mode ends (one PIN pairs one device).

### 5.2 Why the PIN is never sent, not even hashed

A PIN has only 1 000 000 possible values. Anything computed from it and sent over the network (a hash, a proof, a signature)
can be tried against all 1 000 000 values in about a second. Someone sitting between the two devices would collect that value and
recover the PIN offline, then pair as the sender. A slow hash only raises the price. So the PIN is **never** put on the wire in any form
that can be tested offline. Instead the devices reveal it **one bit at a time** with commitments, as in the passkey entry of Bluetooth LE
(only the recipe is borrowed, no Bluetooth is involved), and each commitment also contains **both fingerprints**.

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
  All 20 rounds: 1 chance in 1 048 576 per exchange, at most 3 exchanges per PIN (about 3 in a million).
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

Rules: every error path sends **exactly one** reply and then closes. `file_result` `ok` is sent only after the file is fully
written, its checksum verified, and its final name in place. Existing files are never overwritten.

## 7. Limits and timeouts

| what | limit |
|---|---|
| JSON header | 1 to 16384 bytes |
| TLS handshake | 10 s |
| first message (`hello`) after the handshake | 10 s |
| PIN lifetime (pairing mode) | 120 s |
| failed exchanges per PIN | 3 |
| each pairing message, once round 0 has started | 5 s |
| the whole exchange (20 rounds) | 30 s |
| nothing received for | 30 s while a file is moving (close; a stalled body counts) |
| device name | 64 characters, after cleaning |

## 8. What this protects, and what it does not

Protected:

- Someone listening on the Wi-Fi cannot read the files.
- A device that has never been paired cannot put a file on a receiver.
- A device that cannot see the receiver's screen cannot pair: it does not know the PIN and its blind chance is about 3 in a million per pairing mode.
- A device in the middle during pairing succeeds with the same tiny chance, whether or not the user is careful.
- A paired device is recognised by its certificate, not by its IP address, so a changing IP does not matter.

Not protected:

- Someone who can see the receiver's screen while the PIN is shown can read the PIN.
- Someone on the network can spoil pairing (denial of service) by failing three exchanges, which ends pairing mode. They still cannot pair; the user starts it again.
- A trusted device that is hacked can still send files to you. They only ever land in the receive folder under a new name, never over an existing file.
- Someone can still see *that* two devices talk, and how big the transfers are.
- Anyone who can copy a device's private key file can pretend to be that device. The key file MUST be readable by the owner only.

## 9. Local storage (not part of the wire protocol; for implementers)

In the app's data folder:

- `identity.key` and `identity.crt` (PEM): the device's key and certificate. The key file is owner-only (mode 0600 on Linux).
- `trusted_devices.json`: `{ "schemaVersion": 1, "devices": [ { "fingerprint": "<64 hex>", "name": "...", "pairedAt": "<ISO time>" } ] }`.
  It is written by writing a temporary file and renaming it, so a crash never leaves a half-written list.
- A trusted device can be removed from the list at any time; it then has to pair again.
- The PIN is never saved anywhere.

Which key type to use (for example ECDSA P-256) and how to create the certificate on every operating system is decided by
the proof-of-concept step and is not a wire-protocol matter.
