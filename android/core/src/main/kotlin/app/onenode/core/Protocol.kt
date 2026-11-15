package app.onenode.core

import org.json.JSONException
import org.json.JSONObject
import java.io.EOFException
import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer

// OneNode wire protocol v2 (see docs/PROTOCOL.md). This file = the constants, the timeouts and the framing.
object Protocol {
    const val VERSION = 2
    const val DEFAULT_PORT = 45454
    const val MAX_HEADER = 16384          // a JSON header is 1..16384 bytes
    const val PART_SUFFIX = ".onenode-part" // a file that is still arriving carries this suffix
    const val SERVICE_TYPE = "_onenode._tcp" // DNS-SD service type (the nearby list)
}

// All the waiting limits of PROTOCOL.md section 7, in one place. Tests shrink them.
data class Timeouts(
    val connectMs: Int = 10_000,        // sender: TCP connect
    val handshakeMs: Int = 10_000,      // TLS handshake
    val helloMs: Int = 10_000,          // first message after the handshake
    val firstPairMessageMs: Int = 60_000, // after pairing_required: the user is typing the PIN
    val pairMessageMs: Int = 5_000,     // each pairing message once round 0 began
    val pairTotalMs: Int = 30_000,      // the whole 20-round exchange
    val idleMs: Int = 30_000,           // nothing received while a file is moving
    val pinLifetimeMs: Long = 120_000,  // life of a PIN (pairing mode)
)

// A problem with what the other device sent. `code` is the protocol error code we answer with.
class ProtocolException(val code: String, message: String) : IOException(message)

object Frames {
    // [4 bytes big endian length][UTF-8 JSON]
    fun encode(message: JSONObject): ByteArray {
        val json = message.toString().toByteArray(Charsets.UTF_8)
        require(json.size in 1..Protocol.MAX_HEADER) { "header size ${json.size}" }
        return ByteBuffer.allocate(4 + json.size).putInt(json.size).put(json).array()
    }

    // Reads exactly one frame. Throws ProtocolException("invalid_header") for a bad length or bad JSON,
    // EOFException when the connection ends, SocketTimeoutException (an IOException) when the socket's timeout runs out.
    fun read(input: InputStream): JSONObject {
        val prefix = readFully(input, 4)
        val length = ByteBuffer.wrap(prefix).int // a signed int: a "huge" unsigned value shows up negative, caught below
        if (length < 1 || length > Protocol.MAX_HEADER) {
            throw ProtocolException("invalid_header", "Invalid header size")
        }
        val body = readFully(input, length)
        try {
            return JSONObject(String(body, Charsets.UTF_8))
        } catch (e: JSONException) {
            throw ProtocolException("invalid_header", "The message is not valid JSON")
        }
    }

    fun readFully(input: InputStream, count: Int): ByteArray {
        val bytes = ByteArray(count)
        var done = 0
        while (done < count) {
            val n = input.read(bytes, done, count - done)
            if (n < 0) throw EOFException("The connection was closed")
            done += n
        }
        return bytes
    }
}

// Strict readers for fields that come from the network: a value of the wrong type is "missing", never converted
// (org.json would otherwise turn the string "2" into the number 2, or true into "true").
fun JSONObject.strictString(key: String): String? = opt(key) as? String

fun JSONObject.strictBool(key: String): Boolean? = opt(key) as? Boolean

// A whole number: JSON has only "number", so 2, 2.0 and 2e0 are all the number two; 2.5 is not whole.
// (Android's org.json gives Int/Long/Double, the desktop JVM's gives BigDecimal too: both are handled.)
fun JSONObject.strictLong(key: String): Long? = when (val v = opt(key)) {
    is Int -> v.toLong()
    is Long -> v
    is Number -> try {
        java.math.BigDecimal(v.toString()).longValueExact() // throws if it has a fraction or does not fit
    } catch (e: ArithmeticException) {
        null
    } catch (e: NumberFormatException) {
        null // NaN, Infinity
    }
    else -> null
}

fun message(type: String, build: JSONObject.() -> Unit = {}): JSONObject =
    JSONObject().put("v", Protocol.VERSION).put("type", type).apply(build)
