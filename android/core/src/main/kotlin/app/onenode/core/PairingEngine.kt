package app.onenode.core

import org.json.JSONObject
import java.security.MessageDigest
import java.security.SecureRandom

// The 20-round PIN exchange (PROTOCOL.md section 5.3). Same recipe as the desktop PairingEngine, byte for byte.
// It does no input/output: you ask action() what to do, send makeMessage() or hand incoming messages to accept().
class PairingEngine(
    val role: Role,
    private val pin: Int,
    private val fpSender: ByteArray,
    private val fpReceiver: ByteArray,
    private val nonceSource: ((Int) -> ByteArray)? = null, // tests fix the nonces; null = secure random
) {
    enum class Role { SENDER, RECEIVER }
    enum class Action { SEND_COMMIT, EXPECT_COMMIT, SEND_OPEN, EXPECT_OPEN, DONE, FAILED }
    private enum class Step { COMMIT, OTHER_COMMIT, OPEN, OTHER_OPEN }

    private var round = 0
    private var index = 0 // which of the 4 steps of the round
    private var failed = pin !in 0..MAX_PIN || fpSender.size != 32 || fpReceiver.size != 32
    private var myNonce = ByteArray(0)
    private var otherCommit = ByteArray(0)

    private fun current(): Step {
        // sender: commit, (their commit), open, (their open); receiver: (their commit), commit, (their open), open
        val order = if (role == Role.SENDER) SENDER_ORDER else RECEIVER_ORDER
        return order[index]
    }

    fun action(): Action {
        if (failed) return Action.FAILED
        if (round >= ROUNDS) return Action.DONE
        return when (current()) {
            Step.COMMIT -> Action.SEND_COMMIT
            Step.OTHER_COMMIT -> Action.EXPECT_COMMIT
            Step.OPEN -> Action.SEND_OPEN
            Step.OTHER_OPEN -> Action.EXPECT_OPEN
        }
    }

    // bit i of the PIN, most significant of the 20 bits first
    private fun myBit() = (pin shr (ROUNDS - 1 - round)) and 1

    // SHA-256( "OneNode-pair-v2" || role || fpS || fpR || round(1 byte) || nonce(16) || bit(1 byte) )
    private fun commitFor(fromSender: Boolean, nonce: ByteArray, bit: Int): ByteArray {
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update("OneNode-pair-v2".toByteArray(Charsets.US_ASCII))
        digest.update(if (fromSender) 'S'.code.toByte() else 'R'.code.toByte())
        digest.update(fpSender)
        digest.update(fpReceiver)
        digest.update(round.toByte())
        digest.update(nonce)
        digest.update(bit.toByte())
        return digest.digest()
    }

    private fun advance() {
        if (++index == 4) {
            index = 0
            round++
        }
    }

    // The next message to send; call it only while action() is SEND_COMMIT or SEND_OPEN.
    fun makeMessage(): JSONObject {
        check(!failed && round < ROUNDS) { "pairing is over" }
        val msg = message("")
        msg.put("round", round)
        when (current()) {
            Step.COMMIT -> {
                myNonce = nonceSource?.invoke(round) ?: ByteArray(16).also { RANDOM.nextBytes(it) }
                msg.put("type", "pair_commit")
                msg.put("commit", Names.toHex(commitFor(role == Role.SENDER, myNonce, myBit())))
            }
            Step.OPEN -> {
                msg.put("type", "pair_open")
                msg.put("nonce", Names.toHex(myNonce))
            }
            else -> error("makeMessage called while we should be listening")
        }
        advance()
        return msg
    }

    // A message from the other device. false = the exchange failed (and stays failed).
    fun accept(msg: JSONObject): Boolean {
        if (failed || round >= ROUNDS) return false
        if (msg.strictLong("v") != Protocol.VERSION.toLong()) return fail()
        if (msg.strictLong("round") != round.toLong()) return fail() // wrong or repeated round
        val type = msg.strictString("type")

        return when (current()) {
            Step.OTHER_COMMIT -> {
                val text = msg.strictString("commit")
                if (type != "pair_commit" || text == null || !isHex(text, 64)) return fail()
                otherCommit = Names.fromHex(text)
                advance()
                true
            }
            Step.OTHER_OPEN -> {
                val text = msg.strictString("nonce")
                if (type != "pair_open" || text == null || !isHex(text, 32)) return fail()
                // Rebuild the other side's commitment from the nonce it just revealed, with OUR bit and OUR view of both
                // fingerprints. It matches only if the other side used the same bit and saw the same two devices.
                val otherIsSender = role == Role.RECEIVER
                val expected = commitFor(otherIsSender, Names.fromHex(text), myBit())
                if (!MessageDigest.isEqual(expected, otherCommit)) return fail()
                advance()
                true
            }
            else -> fail() // a message arrived while we should be sending
        }
    }

    private fun fail(): Boolean {
        failed = true
        return false
    }

    companion object {
        const val ROUNDS = 20
        const val MAX_PIN = 999_999
        private val RANDOM = SecureRandom()
        private val SENDER_ORDER = arrayOf(Step.COMMIT, Step.OTHER_COMMIT, Step.OPEN, Step.OTHER_OPEN)
        private val RECEIVER_ORDER = arrayOf(Step.OTHER_COMMIT, Step.COMMIT, Step.OTHER_OPEN, Step.OPEN)

        private fun isHex(text: String, length: Int) =
            text.length == length && text.all { it in '0'..'9' || it in 'a'..'f' }

        // uniform over 0..999999 (nextInt(bound) has no modulo bias)
        fun generatePin(): Int = RANDOM.nextInt(MAX_PIN + 1)

        fun formatPin(pin: Int): String {
            val digits = pin.toString().padStart(6, '0')
            return digits.substring(0, 3) + " " + digits.substring(3)
        }

        // "482913" or "482 913" -> 482913; anything else -> null
        fun parsePin(text: String): Int? {
            val digits = text.replace(" ", "")
            if (digits.length != 6 || !digits.all { it in '0'..'9' }) return null
            return digits.toInt()
        }

        fun fingerprintBytes(hex: String): ByteArray =
            if (Names.isValidFingerprint(hex.lowercase())) Names.fromHex(hex.lowercase()) else ByteArray(0)
    }
}
