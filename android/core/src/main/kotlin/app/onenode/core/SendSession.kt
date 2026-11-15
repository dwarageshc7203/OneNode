package app.onenode.core

import org.json.JSONObject
import java.io.IOException
import java.io.InputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import java.security.MessageDigest
import javax.net.ssl.SSLException
import javax.net.ssl.SSLSocket

// What to send. `open` is called once, when the bytes are needed.
class FileSource(val name: String, val size: Long, val open: () -> InputStream)

sealed interface SendOutcome {
    data class Sent(val fileName: String) : SendOutcome
    data class Failed(val reason: String) : SendOutcome
    data object Cancelled : SendOutcome
}

// The sender asks the user for the PIN through this. It is called on the session's own thread and may block for as long
// as the user needs (the receiver waits 60 seconds). `problem` is set when the previous answer was not 6 digits.
// Return null to give up.
fun interface PinPrompt {
    fun ask(peerName: String, problem: String?): String?
}

interface SendEvents {
    fun onPaired(fingerprint: String, name: String) {}
    fun onProgress(done: Long, total: Long) {}
}

// One connection, one file (PROTOCOL.md section 4). Blocking: call run() from a background thread.
// cancel() can be called from any thread.
class SendSession(
    private val host: String,
    private val port: Int,
    private val identity: Identity,
    private val trust: TrustStore,
    private val ownName: String,
    private val file: FileSource,
    private val pinPrompt: PinPrompt,
    private val events: SendEvents = object : SendEvents {},
    private val expectedFingerprint: String? = null, // what the nearby list claimed; the certificate must agree
    private val timeouts: Timeouts = Timeouts(),
) {
    @Volatile private var socket: Socket? = null
    @Volatile private var cancelled = false
    private var peerName = host
    private var peerFingerprint = ""

    // Thrown inside run() to end it with a message for the user.
    private class Stop(val reason: String) : Exception(reason)

    fun cancel() {
        cancelled = true
        try { socket?.close() } catch (_: IOException) { }
    }

    fun run(): SendOutcome {
        try {
            val ssl = connect()
            socket = ssl
            ssl.use { talk(it) }
            return SendOutcome.Sent(file.name)
        } catch (e: Stop) {
            return if (cancelled) SendOutcome.Cancelled else SendOutcome.Failed(e.reason)
        } catch (e: IOException) {
            return if (cancelled) SendOutcome.Cancelled else SendOutcome.Failed(describe(e))
        }
    }

    private fun describe(e: IOException): String = when (e) {
        is SocketTimeoutException -> "$peerName took too long to answer."
        is SSLException -> "The secure connection failed: ${e.message}"
        else -> "The connection to $peerName was lost."
    }

    // TCP connect, then the TLS handshake; each limited to 10 seconds.
    private fun connect(): SSLSocket {
        val plain = Socket()
        socket = plain
        try {
            plain.connect(InetSocketAddress(host, port), timeouts.connectMs)
            plain.tcpNoDelay = true
            val ssl = Tls.context(identity).socketFactory.createSocket(plain, host, port, true) as SSLSocket
            socket = ssl
            Tls.restrict(ssl)
            ssl.soTimeout = timeouts.handshakeMs
            ssl.startHandshake()
            return ssl
        } catch (e: IOException) {
            try { plain.close() } catch (_: IOException) { }
            if (cancelled) throw Stop("Cancelled")
            if (e is SSLException) throw Stop("The secure connection could not be set up. Is that a OneNode device?")
            throw Stop("No answer from the other device. Check the address, and that OneNode is open there.")
        }
    }

    private fun talk(ssl: SSLSocket) {
        val input = ssl.inputStream
        val output = ssl.outputStream

        // who is on the other end? Read from the certificate of the handshake, never from anything the other side says
        peerFingerprint = Tls.peerFingerprint(ssl) ?: throw Stop("The other device did not present a certificate.")
        // The nearby list only shows what a device SAYS about itself. If the certificate tells another story,
        // somebody is pretending: stop before anything (even our name) is sent.
        if (expectedFingerprint != null && expectedFingerprint != peerFingerprint) {
            throw Stop("This is not the device it claims to be, so nothing was sent. Try again, or pair it again.")
        }

        output.write(Frames.encode(message("hello") {
            put("name", ownName)
            put("known", trust.isTrusted(peerFingerprint)) // do WE have the other device on our list?
        }))
        output.flush()

        ssl.soTimeout = timeouts.helloMs
        val reply = readReply(input)
        if (reply.strictString("type") != "hello_result" || reply.strictLong("v") != Protocol.VERSION.toLong()) {
            throw Stop("Unexpected reply to hello")
        }
        reply.strictString("name")?.let { peerName = Names.cleanName(it) }

        when (reply.strictString("status")) {
            "ok" -> {
                // We only believe "ok" for a device WE have on our list: anything else would let an unknown device
                // (maybe an impostor at this address) receive our file.
                if (!trust.isTrusted(peerFingerprint)) throw Stop("The other device is not paired with this one.")
            }
            "pairing_required" -> pair(ssl, input, output)
            else -> throw Stop(friendlyError(reply.strictString("code") ?: ""))
        }
        sendFile(ssl, input, output)
    }

    // reads one frame from the receiver; a plain "error" reply ends the conversation with its sentence
    private fun readReply(input: InputStream): JSONObject {
        try {
            return Frames.read(input)
        } catch (e: ProtocolException) {
            throw Stop("Invalid reply from receiver")
        }
    }

    private fun pair(ssl: SSLSocket, input: InputStream, output: java.io.OutputStream) {
        // ask the user (this may take a while; the receiver keeps the connection open for 60 s)
        var problem: String? = null
        var pin: Int? = null
        while (pin == null) {
            val answer = pinPrompt.ask(peerName, problem)
            if (cancelled || answer == null) throw Stop("Pairing was cancelled.")
            pin = PairingEngine.parsePin(answer.trim())
            if (pin == null) problem = "The PIN is 6 digits."
        }

        val engine = PairingEngine(
            PairingEngine.Role.SENDER, pin,
            Names.fromHex(identity.fingerprint), PairingEngine.fingerprintBytes(peerFingerprint),
        )
        val startedAt = System.nanoTime()
        ssl.soTimeout = timeouts.pairMessageMs

        while (true) {
            while (engine.action() == PairingEngine.Action.SEND_COMMIT || engine.action() == PairingEngine.Action.SEND_OPEN) {
                output.write(Frames.encode(engine.makeMessage()))
            }
            output.flush()
            when (engine.action()) {
                PairingEngine.Action.FAILED -> throw Stop(friendlyError("pairing_failed"))
                PairingEngine.Action.DONE -> break
                else -> Unit
            }
            // wait for the receiver's next message, but never beyond the 30 s of the whole exchange
            val left = timeouts.pairTotalMs - (System.nanoTime() - startedAt) / 1_000_000
            if (left <= 0) throw Stop(friendlyError("timeout"))
            ssl.soTimeout = minOf(timeouts.pairMessageMs.toLong(), left).toInt()
            val reply = readReply(input)
            // the receiver may end the exchange with an error instead of the message we expect
            if (reply.strictString("type") == "pair_result") throw Stop(friendlyError(reply.strictString("code") ?: ""))
            if (!engine.accept(reply)) throw Stop(friendlyError("pairing_failed"))
        }

        // all 20 rounds passed on our side: now the receiver's verdict
        ssl.soTimeout = timeouts.pairMessageMs
        val result = readReply(input)
        if (result.strictString("type") != "pair_result" || result.strictString("status") != "ok") {
            throw Stop(friendlyError(result.strictString("code") ?: ""))
        }
        // save the receiver BEFORE sending the file, so the next transfer needs no PIN
        trust.add(peerFingerprint, peerName)?.let { throw Stop("Paired, but the list of paired devices could not be saved: $it") }
        events.onPaired(peerFingerprint, peerName)
    }

    private fun sendFile(ssl: SSLSocket, input: InputStream, output: java.io.OutputStream) {
        output.write(Frames.encode(message("file") {
            put("filename", file.name)
            put("size", file.size)
        }))

        val digest = MessageDigest.getInstance("SHA-256")
        val throttle = ProgressThrottle()
        var sent = 0L
        try {
            file.open().use { source ->
                val buffer = ByteArray(64 * 1024)
                events.onProgress(0, file.size)
                while (sent < file.size) {
                    val n = source.read(buffer, 0, minOf(buffer.size.toLong(), file.size - sent).toInt())
                    if (n < 0) throw Stop("The file ended early or could not be read.")
                    digest.update(buffer, 0, n)
                    output.write(buffer, 0, n)
                    sent += n
                    if (throttle.shouldEmit(sent, file.size)) events.onProgress(sent, file.size)
                }
            }
            output.write(Frames.encode(message("file_end") { put("sha256", Names.toHex(digest.digest())) }))
            output.flush()
        } catch (e: IOException) {
            if (e is SSLException || e is ProtocolException || cancelled || !isConnectionProblem(e)) throw e
            // The receiver may have refused us and closed (a full disk, a bad name...). Its reason is more useful
            // than "connection lost", so look for one short reply.
            explainFromReceiver(ssl, input, e)
        }

        ssl.soTimeout = timeouts.idleMs
        val result = readReply(input)
        if (result.strictString("status") == "ok") return
        throw Stop(friendlyError(result.strictString("code") ?: ""))
    }

    private fun isConnectionProblem(e: IOException) = e !is SocketTimeoutException

    private fun explainFromReceiver(ssl: SSLSocket, input: InputStream, original: IOException): Nothing {
        try {
            ssl.soTimeout = 2_000
            val reply = Frames.read(input)
            val code = reply.strictString("code")
            if (code != null) throw Stop(friendlyError(code))
        } catch (e: Stop) {
            throw e
        } catch (_: IOException) {
        }
        throw original
    }

    // Turns the receiver's error code into a sentence the user can act on (same sentences as the desktop app).
    private fun friendlyError(code: String): String = when (code) {
        "pairing_off" -> "This device is not paired with $peerName. On $peerName, choose \"Pair a new device\", then send again."
        "busy" -> "$peerName is busy pairing with another device. Try again in a moment."
        "not_paired" -> "$peerName does not know this device yet."
        "pairing_failed" -> "Pairing failed: the PIN was wrong, or someone is interfering. On $peerName, start pairing again to get a new PIN."
        "pairing_timeout" -> "The PIN has expired. On $peerName, start pairing again to get a new PIN."
        "timeout" -> "$peerName took too long to answer."
        "unsupported" -> "$peerName uses a different version of OneNode."
        "write_failed" -> "$peerName could not save the file (is its storage full?)."
        "checksum_mismatch" -> "The file arrived damaged, so $peerName threw it away. Send it again."
        "invalid_filename" -> "$peerName did not accept the file name."
        else -> "The transfer failed, code: $code"
    }
}
