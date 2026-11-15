package app.onenode

import app.onenode.core.Protocol

// An address typed by hand: "192.168.1.20", "192.168.1.20:45454", "laptop.local", "[fe80::1]:45454"
data class HostPort(val host: String, val port: Int)

object Address {
    private val HOST_NAME = Regex("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$")

    fun parse(text: String): HostPort? {
        val t = text.trim()
        if (t.isEmpty() || t.length > 255) return null

        if (t.startsWith("[")) { // IPv6 in brackets, optional port
            val end = t.indexOf(']')
            if (end < 0) return null
            val host = t.substring(1, end)
            if (!looksLikeIpv6(host)) return null
            val rest = t.substring(end + 1)
            if (rest.isEmpty()) return HostPort(host, Protocol.DEFAULT_PORT)
            if (!rest.startsWith(":")) return null
            val port = parsePort(rest.substring(1)) ?: return null
            return HostPort(host, port)
        }

        if (t.count { it == ':' } > 1) { // bare IPv6, no port possible
            return if (looksLikeIpv6(t)) HostPort(t, Protocol.DEFAULT_PORT) else null
        }

        val host = t.substringBefore(':')
        val port = if (':' in t) parsePort(t.substringAfter(':')) ?: return null else Protocol.DEFAULT_PORT
        return if (HOST_NAME.matches(host)) HostPort(host, port) else null
    }

    private fun parsePort(text: String): Int? {
        if (text.isEmpty() || text.length > 5 || !text.all { it in '0'..'9' }) return null
        return text.toInt().takeIf { it in 1..65535 }
    }

    private fun looksLikeIpv6(text: String) =
        text.contains(':') && text.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' || it == ':' || it == '.' || it == '%' || it.isLetterOrDigit() }
}
