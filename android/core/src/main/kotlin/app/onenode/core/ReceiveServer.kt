package app.onenode.core

import java.io.IOException
import java.net.InetSocketAddress
import java.util.concurrent.Semaphore
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLSocket

// Listens on one TCP port and gives every connection to a ReceiveSession on its own thread.
// At most MAX_CONNECTIONS at once: anybody on the network may open connections, so there is a cap.
class ReceiveServer(
    private val port: Int,
    private val services: ReceiveServices,
    private val events: ReceiveEvents,
) {
    private val slots = Semaphore(MAX_CONNECTIONS)
    private val open = java.util.Collections.synchronizedSet(HashSet<SSLSocket>())
    @Volatile private var server: SSLServerSocket? = null

    // Starts listening. Returns the port actually used (useful when `port` is 0). Throws IOException (for example "port in use").
    fun start(): Int {
        check(server == null) { "already started" }
        val context = Tls.context(services.identity)
        val s = context.serverSocketFactory.createServerSocket() as SSLServerSocket
        try {
            s.reuseAddress = true
            s.needClientAuth = true // mutual TLS: the other device must show its certificate too
            if (Tls.PROTOCOLS.any { it !in s.supportedProtocols }) throw IOException("This system does not support TLS 1.3.")
            s.enabledProtocols = Tls.PROTOCOLS
            s.bind(InetSocketAddress(port), 16)
        } catch (e: IOException) {
            s.close()
            throw e
        }
        server = s
        Thread({ acceptLoop(s) }, "onenode-accept").apply { isDaemon = true }.start()
        return s.localPort
    }

    fun stop() {
        val s = server ?: return
        server = null
        try { s.close() } catch (_: IOException) { }
        synchronized(open) { open.toList() }.forEach { try { it.close() } catch (_: IOException) { } }
    }

    private fun acceptLoop(s: SSLServerSocket) {
        while (!s.isClosed) {
            val client = try {
                s.accept() as SSLSocket
            } catch (e: IOException) {
                if (s.isClosed) return
                continue
            }
            if (!slots.tryAcquire()) {
                try { client.close() } catch (_: IOException) { }
                continue
            }
            open.add(client)
            Thread({
                try {
                    ReceiveSession(client, services, events).run()
                } finally {
                    open.remove(client)
                    slots.release()
                }
            }, "onenode-session").apply { isDaemon = true }.start()
        }
    }

    companion object {
        const val MAX_CONNECTIONS = 8
    }
}
