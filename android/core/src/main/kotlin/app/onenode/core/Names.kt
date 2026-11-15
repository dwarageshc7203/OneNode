package app.onenode.core

import java.security.MessageDigest

// Small text helpers shared by everything: device names from the network, fingerprints and hex.
object Names {
    const val UNKNOWN = "Unknown device"
    private const val MAX_NAME = 64

    // A name comes from the other device = untrusted text. Strip control and format characters, turn any run of
    // spaces into one, cut it to 64 characters, and never return an empty string (same rules as the desktop app).
    fun cleanName(raw: String): String {
        val out = StringBuilder()
        var i = 0
        while (i < raw.length) {
            val cp = raw.codePointAt(i)
            i += Character.charCount(cp)
            if (isSpace(cp)) {
                out.append(' ')
                continue
            }
            val type = Character.getType(cp)
            val bad = type == Character.CONTROL.toInt() || type == Character.FORMAT.toInt() ||
                type == Character.LINE_SEPARATOR.toInt() || type == Character.PARAGRAPH_SEPARATOR.toInt() ||
                type == Character.SURROGATE.toInt() || type == Character.UNASSIGNED.toInt() ||
                type == Character.PRIVATE_USE.toInt() || cp == 0xFFFE || cp == 0xFFFF || cp in 0xFDD0..0xFDEF
            if (!bad) out.appendCodePoint(cp)
        }
        // simplified(): trim and collapse runs of spaces
        var text = out.toString().trim().replace(Regex(" {2,}"), " ")
        if (text.length > MAX_NAME) {
            var end = MAX_NAME
            if (Character.isHighSurrogate(text[end - 1])) end-- // never cut a character in half
            text = text.substring(0, end).trim()
        }
        return text.ifEmpty { UNKNOWN }
    }

    private fun isSpace(cp: Int) = Character.isWhitespace(cp) || Character.isSpaceChar(cp) || cp == 0x85

    // A name for a file that arrives from the network: the protocol already forbids paths, this removes what is still
    // odd on a phone (control characters, backslashes) and keeps it to a sane length.
    fun safeFileName(raw: String): String {
        val sb = StringBuilder()
        raw.forEach { c -> sb.append(if (c.isISOControl() || c == '\\' || c == '\u0000') '_' else c) }
        var name = sb.toString().trim()
        if (name.length > 200) {
            val dot = name.lastIndexOf('.')
            val ext = if (dot > 0 && name.length - dot <= 16) name.substring(dot) else ""
            name = name.substring(0, 200 - ext.length) + ext
        }
        return if (name.isEmpty() || name == "." || name == "..") "file" else name
    }

    private val FINGERPRINT = Regex("^[0-9a-f]{64}$")
    fun isValidFingerprint(text: String) = FINGERPRINT.matches(text)

    private const val DIGITS = "0123456789abcdef"
    fun toHex(bytes: ByteArray): String {
        val sb = StringBuilder(bytes.size * 2)
        for (b in bytes) {
            sb.append(DIGITS[(b.toInt() shr 4) and 0xF]).append(DIGITS[b.toInt() and 0xF])
        }
        return sb.toString()
    }

    fun fromHex(text: String): ByteArray {
        require(text.length % 2 == 0)
        return ByteArray(text.length / 2) { i ->
            ((Character.digit(text[2 * i], 16) shl 4) or Character.digit(text[2 * i + 1], 16)).toByte()
        }
    }

    fun sha256(bytes: ByteArray): ByteArray = MessageDigest.getInstance("SHA-256").digest(bytes)
}
