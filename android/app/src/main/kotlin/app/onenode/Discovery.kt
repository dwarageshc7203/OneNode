package app.onenode

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import app.onenode.core.Names
import app.onenode.core.Protocol
import java.net.Inet4Address
import java.net.InetAddress

// The "nearby devices" list, using Android's own DNS-SD service (NsdManager). It speaks the same mDNS as the desktop app
// (PROTOCOL.md section 8): service _onenode._tcp, TXT  v=2, fp=<fingerprint>, name=<device name>, pair=1 (pairing mode).
// Nothing found this way is trusted; it only saves typing an address. The sender checks the certificate's fingerprint
// against the announced one before it says anything.
class Discovery(
    context: Context,
    private val ownFingerprint: () -> String,
    private val onPeers: (List<Peer>) -> Unit,
) {
    data class Peer(val serviceName: String, val fingerprint: String, val name: String, val host: String, val port: Int, val pairing: Boolean)
    data class Advert(val fingerprint: String, val name: String, val port: Int, val pairing: Boolean)

    private val nsd = context.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val lock = Any()

    // ---- browsing ----
    private var discoveryListener: NsdManager.DiscoveryListener? = null
    private val peers = LinkedHashMap<String, Peer>()   // by service name, at most MAX_PEERS
    private val resolveQueue = ArrayDeque<NsdServiceInfo>()
    private var resolving = false

    fun startBrowsing() {
        synchronized(lock) {
            if (discoveryListener != null) return
            val listener = object : NsdManager.DiscoveryListener {
                override fun onDiscoveryStarted(serviceType: String) {}
                override fun onDiscoveryStopped(serviceType: String) {}
                override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) { synchronized(lock) { discoveryListener = null } }
                override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
                override fun onServiceFound(info: NsdServiceInfo) {
                    synchronized(lock) {
                        if (resolveQueue.size < MAX_PEERS * 2) resolveQueue.add(info)
                    }
                    resolveNext()
                }
                override fun onServiceLost(info: NsdServiceInfo) {
                    val changed = synchronized(lock) { peers.remove(info.serviceName) != null }
                    if (changed) publish()
                }
            }
            discoveryListener = listener
            try {
                nsd.discoverServices(Protocol.SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, listener)
            } catch (_: RuntimeException) {
                discoveryListener = null
            }
        }
    }

    fun stopBrowsing() {
        val listener = synchronized(lock) {
            val l = discoveryListener
            discoveryListener = null
            resolveQueue.clear()
            peers.clear()
            l
        }
        if (listener != null) {
            try { nsd.stopServiceDiscovery(listener) } catch (_: RuntimeException) { }
        }
        onPeers(emptyList())
    }

    // Android resolves one service at a time per app, so they wait in a queue
    @Suppress("DEPRECATION")
    private fun resolveNext() {
        val next = synchronized(lock) {
            if (resolving || resolveQueue.isEmpty()) return
            resolving = true
            resolveQueue.removeFirst()
        }
        try {
            nsd.resolveService(next, object : NsdManager.ResolveListener {
                override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) { resolveDone() }
                override fun onServiceResolved(info: NsdServiceInfo) {
                    accept(info)
                    resolveDone()
                }
            })
        } catch (_: RuntimeException) {
            resolveDone()
        }
    }

    private fun resolveDone() {
        synchronized(lock) { resolving = false }
        resolveNext()
    }

    // Everything from the network is untrusted: only well-formed announcements make it into the list.
    private fun accept(info: NsdServiceInfo) {
        val attrs = info.attributes
        fun text(key: String) = attrs[key]?.toString(Charsets.UTF_8)
        if (text("v") != Protocol.VERSION.toString()) return
        val fp = text("fp") ?: return
        if (!Names.isValidFingerprint(fp) || fp == ownFingerprint()) return
        val port = info.port
        if (port !in 1..65535) return
        val address = pickAddress(info) ?: return
        val peer = Peer(
            serviceName = info.serviceName,
            fingerprint = fp,
            name = Names.cleanName(text("name") ?: ""),
            host = address.hostAddress ?: return,
            port = port,
            pairing = text("pair") == "1",
        )
        synchronized(lock) {
            // a flood of fake devices cannot grow the list without limit
            if (peers.size >= MAX_PEERS && !peers.containsKey(peer.serviceName)) return
            peers[peer.serviceName] = peer
        }
        publish()
    }

    @Suppress("DEPRECATION")
    private fun pickAddress(info: NsdServiceInfo): InetAddress? {
        val all: List<InetAddress> = if (android.os.Build.VERSION.SDK_INT >= 34) info.hostAddresses else listOfNotNull(info.host)
        return all.filter { !it.isLoopbackAddress && !it.isAnyLocalAddress && !it.isMulticastAddress }
            .sortedBy { if (it is Inet4Address) 0 else 1 } // IPv4 first
            .firstOrNull()
    }

    private fun publish() {
        val list = synchronized(lock) { peers.values.toList() }
        onPeers(list)
    }

    // ---- announcing ourselves ----
    private var registrationListener: NsdManager.RegistrationListener? = null
    private var wanted: Advert? = null
    private var registered: Advert? = null
    private var busyRegistering = false

    // null = stop announcing. A changed announcement (new name, pairing mode on/off) replaces the old one.
    fun advertise(advert: Advert?) {
        synchronized(lock) { wanted = advert }
        applyAdvert()
    }

    private fun applyAdvert() {
        synchronized(lock) {
            if (busyRegistering) return // will be called again when the current step is finished
            val want = wanted
            if (registered == want) return

            if (registered != null) { // first take the old one down
                val l = registrationListener
                busyRegistering = true
                try {
                    nsd.unregisterService(l!!)
                } catch (_: RuntimeException) {
                    busyRegistering = false
                    registered = null
                    registrationListener = null
                }
                return
            }
            if (want == null) return

            val info = NsdServiceInfo().apply {
                serviceName = "OneNode-" + want.fingerprint.take(8)
                serviceType = Protocol.SERVICE_TYPE
                port = want.port
                setAttribute("v", Protocol.VERSION.toString())
                setAttribute("fp", want.fingerprint)
                setAttribute("name", cutUtf8(want.name, 240)) // one TXT entry must stay under 255 bytes
                if (want.pairing) setAttribute("pair", "1")
            }
            val listener = object : NsdManager.RegistrationListener {
                override fun onServiceRegistered(info: NsdServiceInfo) = step(true) { registered = want }
                // a failed registration is not retried by itself (that could loop): the next advertise() call tries again
                override fun onRegistrationFailed(info: NsdServiceInfo, errorCode: Int) = step(false) { registrationListener = null }
                override fun onServiceUnregistered(info: NsdServiceInfo) = step(true) { registered = null; registrationListener = null }
                override fun onUnregistrationFailed(info: NsdServiceInfo, errorCode: Int) = step(true) { registered = null; registrationListener = null }
            }
            registrationListener = listener
            busyRegistering = true
            try {
                nsd.registerService(info, NsdManager.PROTOCOL_DNS_SD, listener)
            } catch (_: RuntimeException) {
                busyRegistering = false
                registrationListener = null
            }
        }
    }

    // one registration step finished; if what we want has changed in the meantime, carry on with the next step
    private fun step(carryOn: Boolean, change: () -> Unit) {
        synchronized(lock) {
            change()
            busyRegistering = false
        }
        if (carryOn) applyAdvert()
    }

    private fun cutUtf8(text: String, maxBytes: Int): String {
        var out = text
        while (out.toByteArray(Charsets.UTF_8).size > maxBytes) out = out.dropLast(1)
        return out
    }

    companion object {
        const val MAX_PEERS = 64
    }
}
