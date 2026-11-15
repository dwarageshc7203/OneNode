package app.onenode.core

import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit
import java.util.concurrent.CopyOnWriteArrayList

// "Pair a new device" on the receiving side (PROTOCOL.md section 5.1): a random PIN that lives 120 seconds,
// admits ONE sender at a time, and is burnt by any exchange that began and did not succeed.
class PairingMode(private val scheduler: ScheduledExecutorService, private val lifetimeMs: Long = 120_000) {
    interface Listener {
        fun onStarted(pin: Int) {}
        fun onEnded(reason: String) {}           // "expired", "cancelled", "failed", "paired"
        fun onPaired(fingerprint: String, name: String) {}
    }

    private val lock = Any()
    private val listeners = CopyOnWriteArrayList<Listener>()
    private var active = false
    private var busy = false
    private var exchangeBegan = false
    private var pin = -1
    private var timer: ScheduledFuture<*>? = null

    fun addListener(l: Listener) { listeners.add(l) }
    fun removeListener(l: Listener) { listeners.remove(l) }

    fun isActive(): Boolean = synchronized(lock) { active }
    fun pin(): Int = synchronized(lock) { pin }

    fun start() {
        val newPin: Int
        synchronized(lock) {
            timer?.cancel(false)
            busy = false
            exchangeBegan = false
            pin = PairingEngine.generatePin() // a NEW pin every time, never reused
            newPin = pin
            active = true
            timer = scheduler.schedule({ end("expired") }, lifetimeMs, TimeUnit.MILLISECONDS)
        }
        listeners.forEach { it.onStarted(newPin) }
    }

    fun cancel() {
        if (synchronized(lock) { active }) end("cancelled")
    }

    private fun end(reason: String) {
        synchronized(lock) {
            if (!active) return
            timer?.cancel(false)
            timer = null
            active = false
            busy = false
            exchangeBegan = false
            pin = -1 // the PIN exists nowhere any more
        }
        listeners.forEach { it.onEnded(reason) }
    }

    // a sender needs pairing: true = it may pair now (and holds the single place), false = off or busy
    fun beginAttempt(): Boolean = synchronized(lock) {
        if (!active || busy) return false
        busy = true
        exchangeBegan = false
        true
    }

    fun attemptStartedExchange() = synchronized(lock) { if (active && busy) exchangeBegan = true }

    // the sender left (or its connection broke): before round 0 this only frees the place, after it the PIN is burnt
    fun attemptAbandoned() {
        val burn = synchronized(lock) {
            if (active && busy && !exchangeBegan) { busy = false; false } else active && busy && exchangeBegan
        }
        if (burn) end("failed")
    }

    fun attemptFailed() {
        if (synchronized(lock) { active && busy }) end("failed") // single use: a failed exchange burns the PIN
    }

    fun attemptSucceeded(fingerprint: String, name: String) {
        val ok = synchronized(lock) { active && busy }
        if (ok) {
            end("paired")
            listeners.forEach { it.onPaired(fingerprint, name) }
        }
    }
}
