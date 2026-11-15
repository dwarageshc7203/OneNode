package app.onenode.core

import org.json.JSONObject
import java.io.IOException
import java.net.SocketTimeoutException
import java.security.MessageDigest
import javax.net.ssl.SSLSocket

// What the receiving side needs from the rest of the app.
class ReceiveServices(
    val identity: Identity,
    val ownName: () -> String,      // asked each time, so a rename takes effect at once
    val trust: TrustStore,
    val pairing: PairingMode,
    val sink: FileSink,
    val timeouts: Timeouts = Timeouts(),
)

// Events for the screen. They arrive on the session's own thread.
// `id` tells which connection an event belongs to (two files may arrive at the same time, even with the same name).
interface ReceiveEvents {
    // A device we do not trust must not be able to put anything on screen, so a refusal before trust is only for logs.
    fun onRefused(code: String, reason: String) {}
    fun onStarted(id: Long, fileName: String, size: Long, from: String) {}
    fun onProgress(id: Long, fileName: String, done: Long, total: Long) {}
    fun onFinished(id: Long, fileName: String, savedAs: String, from: String) {}
    fun onFailed(id: Long, fileName: String, reason: String) {}
}

// One incoming connection (PROTOCOL.md sections 4 and 5), blocking. ReceiveServer runs one of these per connection.
class ReceiveSession(
    private val socket: SSLSocket,
    private val services: ReceiveServices,
    private val events: ReceiveEvents,
) : Runnable {
    private enum class Phase { HELLO, FIRST_PAIR, PAIRING, FILE }

    private val id = nextId.incrementAndGet()
    private val timeouts = services.timeouts
    private val writeLock = Any()
    @Volatile private var closing = false
    private var phase = Phase.HELLO
    private var holdsAttempt = false
    private var peerFingerprint = ""
    private var peerName = Names.UNKNOWN
    private var currentName = ""
    private var incoming: FileSink.Incoming? = null

    private class Stop : Exception()

    // The pairing mode ended (PIN expired, or the user cancelled) while this sender was still at the door.
    private val pairingListener = object : PairingMode.Listener {
        override fun onEnded(reason: String) {
            synchronized(writeLock) {
                if (holdsAttempt && !closing) {
                    holdsAttempt = false // the mode is already over: nothing to release
                    replyAndClose("pairing_timeout", "The PIN is no longer valid")
                }
            }
        }
    }

    override fun run() {
        services.pairing.addListener(pairingListener)
        try {
            socket.soTimeout = timeouts.handshakeMs
            socket.startHandshake()
            peerFingerprint = Tls.peerFingerprint(socket) ?: return
            conversation()
        } catch (_: Stop) {
            // already answered and closed
        } catch (e: IOException) {
            if (incoming != null && !closing) events.onFailed(id, currentName, "Connection lost mid-transfer")
        } finally {
            services.pairing.removeListener(pairingListener)
            releaseAttempt() // never leave the pairing door blocked
            incoming?.abort()
            try { socket.close() } catch (_: IOException) { }
        }
    }

    // ---- reading ----

    // One frame, within `ms` milliseconds; too slow = answer "timeout" and stop; bad frame = answer with its code.
    private fun readFrame(ms: Int, badFrameCode: String = "invalid_header"): JSONObject {
        try {
            socket.soTimeout = ms
            return Frames.read(socket.inputStream)
        } catch (e: SocketTimeoutException) {
            reject("timeout", "The other device took too long")
        } catch (e: ProtocolException) {
            reject(badFrameCode, e.message ?: "Invalid message")
        }
    }

    // ---- hello and pairing ----

    private fun conversation() {
        val hello = readFrame(timeouts.helloMs)

        // a device that jumps straight to "file" without introducing itself
        if (hello.strictString("type") == "file") reject("not_paired", "A file was offered before hello")
        if (hello.strictLong("v") != Protocol.VERSION.toLong()) reject("unsupported", "Unsupported protocol version")
        val name = hello.strictString("name")
        if (hello.strictString("type") != "hello" || name == null) reject("invalid_header", "First message must be hello with a name")

        peerName = Names.cleanName(name) // from the network: untrusted text, cleaned
        val senderKnowsUs = hello.strictBool("known") ?: false

        if (services.trust.isTrusted(peerFingerprint) && senderKnowsUs) {
            // an old friend, in both directions: straight to the file
            send(result("hello_result", "ok"))
        } else if (services.pairing.beginAttempt()) {
            // not paired (or only one of us remembers the other): pairing is needed, and only works in pairing mode
            holdsAttempt = true
            send(result("hello_result", "pairing_required"))
            phase = Phase.FIRST_PAIR
            pairing()
        } else {
            reject(if (services.pairing.isActive()) "busy" else "pairing_off", "Pairing refused")
        }

        phase = Phase.FILE
        receiveFile()
    }

    private fun pairing() {
        var engine: PairingEngine? = null
        var startedAt = 0L
        while (true) {
            val first = engine == null
            val ms = if (first) timeouts.firstPairMessageMs else {
                val left = timeouts.pairTotalMs - (System.nanoTime() - startedAt) / 1_000_000
                if (left <= 0) reject("timeout", "The pairing took too long")
                minOf(timeouts.pairMessageMs.toLong(), left).toInt()
            }
            val message = readFrame(ms, "pairing_failed")

            if (engine == null) {
                // round 0 begins now: from here on a failure burns the PIN
                if (!services.pairing.isActive()) reject("pairing_timeout", "The PIN is no longer valid")
                engine = PairingEngine(
                    PairingEngine.Role.RECEIVER, services.pairing.pin(),
                    PairingEngine.fingerprintBytes(peerFingerprint), Names.fromHex(services.identity.fingerprint),
                )
                services.pairing.attemptStartedExchange()
                startedAt = System.nanoTime()
                phase = Phase.PAIRING
            }
            phase = Phase.PAIRING
            if (!engine.accept(message)) reject("pairing_failed", "A pairing check failed")

            // everything the engine wants to send now
            while (engine.action() == PairingEngine.Action.SEND_COMMIT || engine.action() == PairingEngine.Action.SEND_OPEN) {
                send(engine.makeMessage())
            }
            when (engine.action()) {
                PairingEngine.Action.DONE -> break
                PairingEngine.Action.FAILED -> reject("pairing_failed", "A pairing check failed")
                else -> Unit
            }
        }

        // remember the other device BEFORE telling it "ok", so a crash cannot leave one side paired and the other not
        services.trust.add(peerFingerprint, peerName)?.let { reject("write_failed", "Could not save the paired device: $it") }
        synchronized(writeLock) { holdsAttempt = false }
        services.pairing.attemptSucceeded(peerFingerprint, peerName) // closes pairing mode, tells the screen
        send(result("pair_result", "ok"))
    }

    // ---- the file ----

    private fun receiveFile() {
        val header = readFrame(timeouts.idleMs)

        if (header.strictLong("v") != Protocol.VERSION.toLong()) reject("unsupported", "Unsupported protocol version")
        if (header.strictString("type") != "file") reject("unsupported", "Unsupported message type")
        val fileName = header.strictString("filename")
        val size = header.strictLong("size")
        if (fileName == null || size == null) reject("invalid_header", "Header is missing the file name or size")
        if (size < 0) reject("invalid_header", "File size is not a whole number >= 0")
        currentName = fileName
        // a plain file name only: no path (so nothing can be written outside the receive folder)
        if (fileName.isEmpty() || fileName == "." || fileName == ".." || '/' in fileName || '\u0000' in fileName) {
            reject("invalid_filename", "Invalid file name")
        }

        val target = try {
            services.sink.create(fileName, size)
        } catch (e: IOException) {
            reject("write_failed", "Destination file couldn't be accessed: ${e.message}")
        } catch (e: RuntimeException) { // for example a name the file system cannot hold
            reject("write_failed", "Destination file couldn't be accessed: ${e.message}")
        }
        incoming = target
        events.onStarted(id, fileName, size, peerName)
        events.onProgress(id, fileName, 0, size)

        // the body: exactly `size` bytes, hashed while they are written
        val digest = MessageDigest.getInstance("SHA-256")
        val throttle = ProgressThrottle()
        val buffer = ByteArray(64 * 1024)
        var received = 0L
        socket.soTimeout = timeouts.idleMs // "nothing arrives for 30 s" restarts with every read
        val input = socket.inputStream
        while (received < size) {
            val n = try {
                input.read(buffer, 0, minOf(buffer.size.toLong(), size - received).toInt())
            } catch (e: SocketTimeoutException) {
                reject("timeout", "The other device took too long")
            }
            if (n < 0) throw java.io.EOFException("The connection was closed")
            try {
                target.out.write(buffer, 0, n)
            } catch (e: IOException) {
                reject("write_failed", "File write failed: ${e.message}")
            }
            digest.update(buffer, 0, n)
            received += n
            if (throttle.shouldEmit(received, size)) events.onProgress(id, fileName, received, size)
        }

        val trailer = readFrame(timeouts.idleMs)
        val claimed = trailer.strictString("sha256")
        if (trailer.strictString("type") != "file_end" || claimed == null) {
            reject("invalid_header", "Trailer must be type file_end with a sha256 string")
        }
        val actual = Names.toHex(digest.digest())
        if (claimed.lowercase() != actual) {
            reject("checksum_mismatch", "Checksum mismatch: sender said ${claimed.lowercase()}, received file is $actual")
        }

        val savedAs = try {
            target.commit()
        } catch (e: IOException) {
            reject("write_failed", "Could not save the file: ${e.message}")
        }
        incoming = null
        send(result("file_result", "ok"))
        events.onFinished(id, fileName, savedAs, peerName)

        // The sender closes after our reply. Waiting a moment for that (instead of slamming the door) keeps our
        // reply from being lost to a connection reset.
        try {
            socket.soTimeout = 2_000
            while (socket.inputStream.read(buffer) >= 0) { /* discard */ }
        } catch (_: IOException) {
        }
    }

    // ---- answering ----

    private fun result(type: String, status: String, code: String? = null): JSONObject = message(type) {
        put("status", status)
        if (type == "hello_result") put("name", services.ownName()) // so the other device can show who it reached
        if (code != null) put("code", code)
    }

    private fun send(message: JSONObject) {
        synchronized(writeLock) {
            if (closing) throw Stop()
            socket.outputStream.write(Frames.encode(message))
            socket.outputStream.flush()
        }
    }

    // Answers with an error (exactly one reply per connection), closes, and ends the session.
    private fun reject(code: String, reason: String): Nothing {
        synchronized(writeLock) { replyAndClose(code, reason) }
        throw Stop()
    }

    // must be called with writeLock held
    private fun replyAndClose(code: String, reason: String) {
        if (closing) return
        closing = true

        // A device that is not (yet) trusted must not be able to put anything on our screen: for it we only log.
        // Once trusted, a failure is a real event the user may want to see.
        if (phase == Phase.FILE) events.onFailed(id, currentName, reason) else events.onRefused(code, reason)

        // "not_paired" answers a file offered too early, so it goes in a file_result
        val type = when {
            code == "not_paired" -> "file_result"
            phase == Phase.HELLO -> "hello_result"
            phase == Phase.FIRST_PAIR || phase == Phase.PAIRING -> "pair_result"
            else -> "file_result"
        }
        try {
            socket.outputStream.write(Frames.encode(result(type, "error", code)))
            socket.outputStream.flush()
        } catch (_: IOException) {
        }
        releaseAttemptLocked()
        try { socket.close() } catch (_: IOException) { }
    }

    private fun releaseAttempt() = synchronized(writeLock) { releaseAttemptLocked() }

    // gives the place at the pairing door back; PairingMode burns the PIN itself if the exchange had begun
    private fun releaseAttemptLocked() {
        if (!holdsAttempt) return
        holdsAttempt = false
        services.pairing.attemptAbandoned()
    }

    private companion object {
        val nextId = java.util.concurrent.atomic.AtomicLong()
    }
}
