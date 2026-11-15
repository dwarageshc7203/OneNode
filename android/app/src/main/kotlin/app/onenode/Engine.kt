package app.onenode

import android.app.Application
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.PowerManager
import androidx.core.content.ContextCompat
import app.onenode.core.FileSink
import app.onenode.core.Identity
import app.onenode.core.Names
import app.onenode.core.PairingEngine
import app.onenode.core.PairingMode
import app.onenode.core.PinPrompt
import app.onenode.core.Protocol
import app.onenode.core.ReceiveEvents
import app.onenode.core.ReceiveServer
import app.onenode.core.ReceiveServices
import app.onenode.core.SendEvents
import app.onenode.core.SendOutcome
import app.onenode.core.SendSession
import app.onenode.core.Timeouts
import app.onenode.core.TrustStore
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.io.File
import java.io.IOException
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong

// Where to send: an address, and (when it came from the nearby list) who it claims to be.
data class Target(val host: String, val port: Int, val label: String, val fingerprint: String?)

enum class TransferState { WAITING, PAIRING, RUNNING, DONE, FAILED, CANCELLED }

data class TransferItem(
    val id: Long,
    val sending: Boolean,
    val fileName: String,
    val peer: String,
    val done: Long = 0,
    val total: Long = 0,
    val state: TransferState = TransferState.WAITING,
    val message: String = "",   // the error, or where the file was saved
)

class PinRequest(val id: Long, val peerName: String, val problem: String?, val answer: CompletableFuture<String?>)

class PairingUi(val pinText: String, val endsAtMs: Long)

// Everything that is not drawing: the identity, the list of paired devices, the receive server, the sends, discovery.
// The screen only reads its StateFlows and calls its functions. It is plain Kotlin on top of the tested :core module.
class Engine(private val app: Application) {
    private val prefs = app.getSharedPreferences("onenode", Context.MODE_PRIVATE)
    private val dataDir = File(app.filesDir, "onenode").apply { mkdirs() }
    private val scheduler = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "onenode-timers").apply { isDaemon = true } }
    private val workers = Executors.newCachedThreadPool { r -> Thread(r, "onenode-send").apply { isDaemon = true } }
    private val nextId = AtomicLong(1)

    // ---- state the screen shows ----
    private val _name = MutableStateFlow(Names.cleanName(prefs.getString("deviceName", null) ?: Build.MODEL ?: "Android phone"))
    val name: StateFlow<String> = _name.asStateFlow()

    private val _receiving = MutableStateFlow(false)
    val receiving: StateFlow<Boolean> = _receiving.asStateFlow()

    private val _problem = MutableStateFlow<String?>(null) // a problem the user must know about (shown in red)
    val problem: StateFlow<String?> = _problem.asStateFlow()

    private val _pairing = MutableStateFlow<PairingUi?>(null)
    val pairing: StateFlow<PairingUi?> = _pairing.asStateFlow()

    private val _nearby = MutableStateFlow<List<Discovery.Peer>>(emptyList())
    val nearby: StateFlow<List<Discovery.Peer>> = _nearby.asStateFlow()

    private val _paired = MutableStateFlow<List<TrustStore.Device>>(emptyList())
    val paired: StateFlow<List<TrustStore.Device>> = _paired.asStateFlow()

    private val _transfers = MutableStateFlow<List<TransferItem>>(emptyList())
    val transfers: StateFlow<List<TransferItem>> = _transfers.asStateFlow()

    private val _pinRequest = MutableStateFlow<PinRequest?>(null)
    val pinRequest: StateFlow<PinRequest?> = _pinRequest.asStateFlow()

    private val _shared = MutableStateFlow<List<Uri>>(emptyList()) // files handed over by the share sheet, waiting for a target
    val shared: StateFlow<List<Uri>> = _shared.asStateFlow()

    private val _toasts = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val toasts: SharedFlow<String> = _toasts.asSharedFlow()

    // ---- the pieces ----
    private val identity: Identity? = try {
        AndroidIdentity.load()
    } catch (e: Exception) {
        _problem.value = "This phone could not create its device key (${e.message}). OneNode cannot send or receive."
        null
    }
    val fingerprint: String get() = identity?.fingerprint ?: ""

    private val trust: TrustStore = TrustStore(File(dataDir, "trusted_devices.json")) { _paired.value = trustList() }
    private val pairingMode = PairingMode(scheduler)
    private val sink: FileSink = MediaStoreSink(app.contentResolver)
    private var server: ReceiveServer? = null
    private val discovery = Discovery(app, { fingerprint }) { _nearby.value = it }
    private val sessions = ConcurrentHashMap<Long, SendSession>()
    private val busy = AtomicInteger(0)
    private var wakeLock: PowerManager.WakeLock? = null

    init {
        trust.load()?.let { _problem.value = it }
        _paired.value = trustList()
        pairingMode.addListener(object : PairingMode.Listener {
            override fun onStarted(pin: Int) {
                _pairing.value = PairingUi(PairingEngine.formatPin(pin), System.currentTimeMillis() + Timeouts().pinLifetimeMs)
                announce()
            }

            override fun onEnded(reason: String) {
                _pairing.value = null
                announce()
                when (reason) {
                    "expired" -> toast("The PIN expired. Start pairing again for a new one.")
                    "failed" -> toast("Pairing failed. Start pairing again for a new PIN.")
                }
            }

            override fun onPaired(fingerprint: String, name: String) = toast("Paired with $name")
        })
    }

    private fun trustList(): List<TrustStore.Device> = trust.devices().sortedBy { it.name.lowercase() }
    private fun toast(text: String) { _toasts.tryEmit(text) }

    // ---- receiving ----

    private val receiveEvents = object : ReceiveEvents {
        override fun onStarted(id: Long, fileName: String, size: Long, from: String) {
            busyStart()
            addItem(TransferItem(id, false, fileName, from, 0, size, TransferState.RUNNING))
        }

        override fun onProgress(id: Long, fileName: String, done: Long, total: Long) = updateItem(id) { it.copy(done = done, total = total) }

        override fun onFinished(id: Long, fileName: String, savedAs: String, from: String) {
            updateItem(id) { it.copy(done = it.total, state = TransferState.DONE, message = "Saved in Downloads/OneNode as $savedAs") }
            busyEnd()
        }

        override fun onFailed(id: Long, fileName: String, reason: String) {
            updateItem(id) { it.copy(state = TransferState.FAILED, message = reason) }
            busyEnd()
        }
    }

    // Starts the server and the "receiving" notification. Returns false (and sets `problem`) if that did not work.
    fun startReceiving(): Boolean {
        if (identity == null) return false
        if (server != null) return true
        val s = ReceiveServer(Protocol.DEFAULT_PORT, ReceiveServices(identity, { _name.value }, trust, pairingMode, sink), receiveEvents)
        try {
            s.start()
        } catch (e: IOException) {
            _problem.value = "OneNode could not start receiving: ${e.message}. Is another copy of OneNode using port ${Protocol.DEFAULT_PORT}?"
            return false
        }
        server = s
        _problem.value = null
        _receiving.value = true
        prefs.edit().putBoolean("receiving", true).apply()
        announce()
        ContextCompat.startForegroundService(app, Intent(app, ReceiveService::class.java))
        return true
    }

    fun stopReceiving() {
        pairingMode.cancel()
        server?.stop()
        server = null
        _receiving.value = false
        prefs.edit().putBoolean("receiving", false).apply()
        announce()
        app.stopService(Intent(app, ReceiveService::class.java))
    }

    fun wantedReceiving() = prefs.getBoolean("receiving", false)

    // ---- pairing a new device (the receiving side) ----

    fun startPairing() {
        if (!startReceiving()) return
        pairingMode.start()
    }

    fun cancelPairing() = pairingMode.cancel()

    private fun announce() {
        discovery.advertise(
            if (server == null || identity == null) null
            else Discovery.Advert(identity.fingerprint, _name.value, Protocol.DEFAULT_PORT, pairingMode.isActive()),
        )
    }

    // ---- discovery follows the screen: searching only while the app is visible (saves battery) ----

    fun screenShown() = discovery.startBrowsing()
    fun screenHidden() = discovery.stopBrowsing()

    // ---- settings ----

    fun rename(newName: String) {
        val clean = Names.cleanName(newName)
        _name.value = clean
        prefs.edit().putString("deviceName", clean).apply()
        announce()
    }

    fun forget(fingerprint: String) {
        trust.remove(fingerprint)?.let { toast(it) }
    }

    fun dismissProblem() { _problem.value = null }

    // ---- sending ----

    fun setShared(uris: List<Uri>) { _shared.value = uris }

    fun sendShared(target: Target) {
        val uris = _shared.value
        _shared.value = emptyList()
        send(uris, target)
    }

    // Sends the files one after the other. The first goes alone: if the device is new, the PIN is asked once,
    // and the others then need none. If the first fails, the rest are not tried.
    fun send(uris: List<Uri>, target: Target) {
        if (identity == null || uris.isEmpty()) return
        val items = uris.map { uri ->
            val d = try { Sources.describe(app.contentResolver, uri) } catch (e: Exception) { Sources.Described("file", -1) }
            TransferItem(nextId.getAndIncrement(), true, d.name, target.label, 0, maxOf(d.size, 0))
        }
        items.forEach { addItem(it) }
        workers.execute { runBatch(uris, items, target) }
    }

    private fun runBatch(uris: List<Uri>, items: List<TransferItem>, target: Target) {
        busyStart()
        try {
            for ((index, uri) in uris.withIndex()) {
                val item = items[index]
                val outcome = sendOne(uri, item, target)
                if (outcome !is SendOutcome.Sent) {
                    for (rest in items.drop(index + 1)) {
                        updateItem(rest.id) { it.copy(state = TransferState.CANCELLED, message = "Not sent: the file before it did not go through.") }
                    }
                    return
                }
            }
        } finally {
            Sources.clearCache(app.cacheDir)
            busyEnd()
        }
    }

    private fun sendOne(uri: Uri, item: TransferItem, target: Target): SendOutcome {
        val source = try {
            Sources.open(app.contentResolver, uri, app.cacheDir)
        } catch (e: Exception) {
            val reason = "The file could not be read: ${e.message ?: "unknown error"}"
            updateItem(item.id) { it.copy(state = TransferState.FAILED, message = reason) }
            return SendOutcome.Failed(reason)
        }
        updateItem(item.id) { it.copy(total = source.size, state = TransferState.RUNNING) }

        val session = SendSession(
            host = target.host, port = target.port, identity = identity!!, trust = trust, ownName = _name.value, file = source,
            pinPrompt = PinPrompt { peerName, problem -> askPin(item.id, peerName, problem) },
            events = object : SendEvents {
                override fun onProgress(done: Long, total: Long) = updateItem(item.id) { it.copy(done = done, total = total) }
                override fun onPaired(fingerprint: String, name: String) = toast("Paired with $name")
            },
            expectedFingerprint = target.fingerprint,
        )
        sessions[item.id] = session
        val outcome = try {
            session.run()
        } finally {
            sessions.remove(item.id)
            _pinRequest.update { if (it?.id == item.id) null else it }
        }
        updateItem(item.id) {
            when (outcome) {
                is SendOutcome.Sent -> it.copy(done = it.total, state = TransferState.DONE, message = "Sent to ${target.label}")
                is SendOutcome.Failed -> it.copy(state = TransferState.FAILED, message = outcome.reason)
                SendOutcome.Cancelled -> it.copy(state = TransferState.CANCELLED, message = "Cancelled")
            }
        }
        return outcome
    }

    // Runs on the sending thread: shows the PIN dialog and waits for the user.
    private fun askPin(id: Long, peerName: String, problem: String?): String? {
        updateItem(id) { it.copy(state = TransferState.PAIRING) }
        val request = PinRequest(id, peerName, problem, CompletableFuture())
        _pinRequest.value = request
        return try {
            request.answer.get()
        } catch (e: InterruptedException) {
            null
        } finally {
            updateItem(id) { if (it.state == TransferState.PAIRING) it.copy(state = TransferState.RUNNING) else it }
        }
    }

    fun submitPin(request: PinRequest, text: String) {
        _pinRequest.update { if (it === request) null else it }
        request.answer.complete(text)
    }

    fun cancelPin(request: PinRequest) {
        _pinRequest.update { if (it === request) null else it }
        request.answer.complete(null)
    }

    fun cancelTransfer(id: Long) {
        sessions[id]?.cancel()
        _pinRequest.value?.takeIf { it.id == id }?.let { cancelPin(it) }
    }

    fun clearFinished() {
        _transfers.update { list -> list.filter { it.state == TransferState.RUNNING || it.state == TransferState.WAITING || it.state == TransferState.PAIRING } }
    }

    // ---- the list of transfers ----

    private fun addItem(item: TransferItem) = _transfers.update { (listOf(item) + it).take(MAX_ITEMS) }

    private fun updateItem(id: Long, change: (TransferItem) -> TransferItem) =
        _transfers.update { list -> list.map { if (it.id == id) change(it) else it } }

    // ---- keep the phone awake only while something is moving ----

    private fun busyStart() {
        if (busy.getAndIncrement() == 0) {
            val pm = app.getSystemService(Context.POWER_SERVICE) as PowerManager
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "OneNode:transfer").apply { acquire(30 * 60 * 1000L) }
        }
    }

    private fun busyEnd() {
        if (busy.decrementAndGet() == 0) {
            wakeLock?.let { if (it.isHeld) it.release() }
            wakeLock = null
        }
    }

    // Used by the notification's "Stop" button and by Android when the service has to end.
    fun onServiceStopped() {
        if (server != null) stopReceiving()
    }

    companion object {
        const val MAX_ITEMS = 40
    }
}
