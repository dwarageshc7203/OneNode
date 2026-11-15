package app.onenode.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class TrustAndModeTest {
    private val fp1 = "a1".repeat(32)
    private val fp2 = "b2".repeat(32)

    private fun store(dir: File = TestSupport.tempDir()) = TrustStore(File(dir, "trusted_devices.json"))

    @Test fun addPersistsAndReloads() {
        val dir = TestSupport.tempDir()
        val a = store(dir)
        assertNull(a.load())
        assertFalse(a.isTrusted(fp1))
        assertNull(a.add(fp1, "Laptop"))
        assertTrue(a.isTrusted(fp1))
        assertTrue(a.isTrusted(fp1.uppercase()))

        val b = store(dir)
        assertNull(b.load())
        assertEquals("Laptop", b.nameOf(fp1))
        assertEquals(1, b.devices().size)
    }

    @Test fun addingTwiceKeepsOneEntryAndFirstTime() {
        val s = store(); s.load()
        s.add(fp1, "Old")
        val first = s.devices().single().pairedAt
        s.add(fp1, "New name")
        assertEquals(1, s.devices().size)
        assertEquals("New name", s.nameOf(fp1))
        assertEquals(first, s.devices().single().pairedAt)
    }

    @Test fun removeAndRejectInvalid() {
        val s = store(); s.load()
        s.add(fp1, "A"); s.add(fp2, "B")
        assertNull(s.remove(fp1))
        assertFalse(s.isTrusted(fp1))
        assertTrue(s.isTrusted(fp2))
        assertNotNull(s.add("not-a-fingerprint", "x"))
    }

    @Test fun namesAreCleaned() {
        val s = store(); s.load()
        s.add(fp1, "  Bad\u0000 ‮name  ")
        assertEquals("Bad name", s.nameOf(fp1))
    }

    @Test fun damagedFileIsKeptAndListStaysReadOnly() {
        val dir = TestSupport.tempDir()
        val file = File(dir, "trusted_devices.json")
        file.writeText("{ this is not json")
        val s = TrustStore(file)
        assertNotNull(s.load())
        assertNotNull(s.add(fp1, "A"))
        assertEquals("{ this is not json", file.readText())
        assertTrue(s.devices().isEmpty())
    }

    @Test fun foreignVersionAndBadEntriesAreRefused() {
        val dir = TestSupport.tempDir()
        val file = File(dir, "trusted_devices.json")
        file.writeText("""{"schemaVersion":2,"devices":[]}""")
        assertNotNull(TrustStore(file).load())
        file.writeText("""{"schemaVersion":1,"devices":[{"fingerprint":"zz","name":"x"}]}""")
        assertNotNull(TrustStore(file).load())
    }

    @Test fun fileFormatMatchesTheDesktopApp() {
        val dir = TestSupport.tempDir()
        val s = store(dir); s.load(); s.add(fp1, "Laptop")
        val root = org.json.JSONObject(File(dir, "trusted_devices.json").readText())
        assertEquals(1, root.getInt("schemaVersion"))
        val d = root.getJSONArray("devices").getJSONObject(0)
        assertEquals(fp1, d.getString("fingerprint"))
        assertEquals("Laptop", d.getString("name"))
        assertTrue(d.getString("pairedAt").startsWith("20"))
        assertFalse(File(dir, "trusted_devices.json.tmp").exists())
    }

    // ---- pairing mode ----

    private fun mode(lifetimeMs: Long = 120_000) = PairingMode(TestSupport.scheduler, lifetimeMs)

    @Test fun startGivesAPinAndCancelRemovesIt() {
        val m = mode()
        assertFalse(m.isActive())
        m.start()
        assertTrue(m.isActive())
        assertTrue(m.pin() in 0..999_999)
        m.cancel()
        assertFalse(m.isActive())
        assertEquals(-1, m.pin())
    }

    @Test fun oneSenderAtATime() {
        val m = mode(); m.start()
        assertTrue(m.beginAttempt())
        assertFalse(m.beginAttempt())
        m.attemptAbandoned() // left before round 0: the place is free again, the PIN lives on
        assertTrue(m.isActive())
        assertTrue(m.beginAttempt())
    }

    @Test fun anExchangeThatBeganAndStoppedBurnsThePin() {
        val m = mode(); m.start()
        assertTrue(m.beginAttempt())
        m.attemptStartedExchange()
        m.attemptAbandoned()
        assertFalse(m.isActive())
    }

    @Test fun successEndsTheModeAndTellsListeners() {
        val m = mode(); m.start()
        var paired: String? = null
        var ended: String? = null
        m.addListener(object : PairingMode.Listener {
            override fun onPaired(fingerprint: String, name: String) { paired = fingerprint }
            override fun onEnded(reason: String) { ended = reason }
        })
        m.beginAttempt(); m.attemptStartedExchange()
        m.attemptSucceeded(fp1, "X")
        assertEquals(fp1, paired)
        assertEquals("paired", ended)
        assertFalse(m.isActive())
        assertFalse(m.beginAttempt())
    }

    @Test fun pinExpires() {
        val m = mode(lifetimeMs = 100)
        val latch = CountDownLatch(1)
        m.addListener(object : PairingMode.Listener { override fun onEnded(reason: String) { if (reason == "expired") latch.countDown() } })
        m.start()
        assertTrue(latch.await(5, TimeUnit.SECONDS))
        assertFalse(m.isActive())
    }

    @Test fun startingAgainMakesANewPinAndResetsTheDoor() {
        val m = mode(); m.start()
        m.beginAttempt()
        m.start()
        assertTrue(m.beginAttempt())
    }
}
