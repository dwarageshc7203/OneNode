package app.onenode.core

import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.security.MessageDigest
import java.util.Collections
import java.util.Random
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLSocket

// Whole conversations over real TLS 1.3 on the loopback interface: a Kotlin sender talking to a Kotlin receiver.
// (InteropTest does the same against the desktop program.)
class SessionTest {
    private class Recorder : ReceiveEvents {
        val refused = Collections.synchronizedList(ArrayList<String>())
        val finished = Collections.synchronizedList(ArrayList<String>())
        val failed = Collections.synchronizedList(ArrayList<String>())
        val started = Collections.synchronizedList(ArrayList<String>())
        override fun onRefused(code: String, reason: String) { refused.add(code) }
        override fun onStarted(id: Long, fileName: String, size: Long, from: String) { started.add(fileName) }
        override fun onFinished(id: Long, fileName: String, savedAs: String, from: String) { finished.add(savedAs) }
        override fun onFailed(id: Long, fileName: String, reason: String) { failed.add(reason) }
    }

    private val recvId = TestSupport.identity("receiver")
    private val sendId = TestSupport.identity("sender")
    private lateinit var recvDir: File
    private lateinit var recvTrust: TrustStore
    private lateinit var sendTrust: TrustStore
    private lateinit var mode: PairingMode
    private lateinit var recorder: Recorder
    private lateinit var server: ReceiveServer
    private var port = 0
    private var timeouts = Timeouts()

    @Before fun setUp() = startServer(Timeouts())

    private fun startServer(t: Timeouts) {
        timeouts = t
        recvDir = TestSupport.tempDir()
        recvTrust = TrustStore(File(TestSupport.tempDir(), "trusted.json")).also { it.load() }
        sendTrust = TrustStore(File(TestSupport.tempDir(), "trusted.json")).also { it.load() }
        mode = PairingMode(TestSupport.scheduler)
        recorder = Recorder()
        server = ReceiveServer(0, ReceiveServices(recvId, { "Test Receiver" }, recvTrust, mode, DirectorySink(recvDir), t), recorder)
        port = server.start()
    }

    @After fun tearDown() { server.stop(); mode.cancel() }

    private fun source(name: String, bytes: ByteArray) = FileSource(name, bytes.size.toLong()) { ByteArrayInputStream(bytes) }

    private fun send(
        name: String = "hello.txt", bytes: ByteArray = "hello onenode".toByteArray(),
        pin: String? = null, expected: String? = null, prompt: PinPrompt? = null,
    ): SendOutcome = SendSession(
        "127.0.0.1", port, sendId, sendTrust, "Test Sender", source(name, bytes),
        prompt ?: PinPrompt { _, _ -> pin ?: error("a PIN was asked but none was given") },
        expectedFingerprint = expected, timeouts = timeouts,
    ).run()

    private fun currentPin() = PairingEngine.formatPin(mode.pin())

    // a hand-made client, to say things the real sender never would
    private fun raw(identity: Identity = sendId): SSLSocket {
        val s = Tls.context(identity).socketFactory.createSocket("127.0.0.1", port) as SSLSocket
        Tls.restrict(s)
        s.soTimeout = 5000
        s.startHandshake()
        return s
    }

    private fun SSLSocket.say(m: JSONObject) { outputStream.write(Frames.encode(m)); outputStream.flush() }
    private fun SSLSocket.hear(): JSONObject = Frames.read(inputStream)

    private fun pairNow() {
        mode.start()
        assertEquals(SendOutcome.Sent("pair.txt"), send(name = "pair.txt", pin = currentPin()))
    }

    // ---- the good paths ----

    @Test fun firstContactPairsThenSendsTheFile() {
        mode.start()
        val outcome = send(pin = currentPin())
        assertEquals(SendOutcome.Sent("hello.txt"), outcome)
        assertEquals("hello onenode", File(recvDir, "hello.txt").readText())
        assertTrue(sendTrust.isTrusted(recvId.fingerprint))
        assertTrue(recvTrust.isTrusted(sendId.fingerprint))
        assertEquals("Test Receiver", sendTrust.nameOf(recvId.fingerprint))
        assertEquals("Test Sender", recvTrust.nameOf(sendId.fingerprint))
        TestSupport.eventually("pairing mode ends after one pairing") { !mode.isActive() }
        TestSupport.eventually("receiver reports the file") { recorder.finished == listOf("hello.txt") }
    }

    @Test fun pairedDevicesSendWithoutAPin() {
        pairNow()
        val outcome = send(name = "second.txt", prompt = PinPrompt { _, _ -> error("the PIN must not be asked again") })
        assertEquals(SendOutcome.Sent("second.txt"), outcome)
        assertTrue(File(recvDir, "second.txt").exists())
    }

    @Test fun neverOverwritesAnExistingFile() {
        pairNow()
        File(recvDir, "same.txt").writeText("original")
        send(name = "same.txt", bytes = "new".toByteArray())
        send(name = "same.txt", bytes = "newer".toByteArray())
        assertEquals("original", File(recvDir, "same.txt").readText())
        assertEquals("new", File(recvDir, "same(1).txt").readText())
        assertEquals("newer", File(recvDir, "same(2).txt").readText())
    }

    @Test fun emptyAndBigAndUnicodeFiles() {
        pairNow()
        assertEquals(SendOutcome.Sent("empty"), send(name = "empty", bytes = ByteArray(0)))
        assertEquals(0L, File(recvDir, "empty").length())

        val big = ByteArray(6 * 1024 * 1024 + 123).also { Random(42).nextBytes(it) }
        assertEquals(SendOutcome.Sent("big.bin"), send(name = "big.bin", bytes = big))
        assertEquals(hex(big), hex(File(recvDir, "big.bin").readBytes()))

        assertEquals(SendOutcome.Sent("Café ✓ 写真.txt"), send(name = "Café ✓ 写真.txt"))
        assertTrue(File(recvDir, "Café ✓ 写真.txt").exists())
        assertTrue("no half-written files left", recvDir.list()!!.none { it.endsWith(Protocol.PART_SUFFIX) })
    }

    private fun hex(b: ByteArray) = Names.toHex(MessageDigest.getInstance("SHA-256").digest(b))

    @Test fun receiverForgotYouMeansPairAgain() {
        pairNow()
        recvTrust.remove(sendId.fingerprint)            // the receiver forgets the sender
        val outcome = send(name = "again.txt", prompt = PinPrompt { _, _ -> error("pairing mode is off, no PIN can be asked") })
        assertTrue(outcome is SendOutcome.Failed && outcome.reason.contains("not paired"))

        mode.start()
        assertEquals(SendOutcome.Sent("again.txt"), send(name = "again.txt", pin = currentPin()))
    }

    @Test fun senderForgotReceiverMeansPairAgain() {
        pairNow()
        sendTrust.remove(recvId.fingerprint)             // hello says known=false although the receiver still trusts us
        mode.start()
        assertEquals(SendOutcome.Sent("x.txt"), send(name = "x.txt", pin = currentPin()))
        assertTrue(sendTrust.isTrusted(recvId.fingerprint))
    }

    // ---- refusals ----

    @Test fun strangersCannotPairWhenPairingModeIsOff() {
        val outcome = send(prompt = PinPrompt { _, _ -> error("no PIN prompt expected") })
        assertTrue(outcome is SendOutcome.Failed && outcome.reason.contains("Pair a new device"))
        assertEquals(listOf("pairing_off"), recorder.refused)
        assertTrue("nothing may reach the screen for an unknown device", recorder.started.isEmpty() && recorder.failed.isEmpty())
        assertFalse(recvTrust.isTrusted(sendId.fingerprint))
    }

    @Test fun wrongPinFailsAndBurnsIt() {
        mode.start()
        val wrong = PairingEngine.formatPin((mode.pin() + 1) % 1_000_000)
        val outcome = send(pin = wrong)
        assertTrue(outcome.toString(), outcome is SendOutcome.Failed && outcome.reason.contains("Pairing failed"))
        TestSupport.eventually("a failed exchange burns the PIN") { !mode.isActive() }
        assertFalse(recvTrust.isTrusted(sendId.fingerprint))
        assertFalse(sendTrust.isTrusted(recvId.fingerprint))
        assertTrue(recvDir.list().isNullOrEmpty())
    }

    @Test fun leavingBeforeTheFirstRoundDoesNotBurnThePin() {
        mode.start()
        val outcome = send(prompt = PinPrompt { _, _ -> null }) // the user cancels at the PIN prompt
        assertEquals(SendOutcome.Failed("Pairing was cancelled."), outcome)
        Thread.sleep(300)
        assertTrue(mode.isActive())
        assertTrue("the door is free again", mode.beginAttempt())
    }

    @Test fun aBadlyTypedPinIsAskedAgain() {
        mode.start()
        val answers = ArrayDeque(listOf("12", "abcdef", currentPin()))
        val problems = ArrayList<String?>()
        val outcome = send(prompt = PinPrompt { _, problem -> problems.add(problem); answers.removeFirst() })
        assertEquals(SendOutcome.Sent("hello.txt"), outcome)
        assertEquals(listOf(null, "The PIN is 6 digits.", "The PIN is 6 digits."), problems)
    }

    @Test fun onlyOneSenderPairsAtATime() {
        mode.start()
        val hold = raw()
        hold.say(message("hello") { put("name", "Slow"); put("known", false) })
        assertEquals("pairing_required", hold.hear().getString("status"))

        val other = TestSupport.identity("third")
        val outcome = SendSession("127.0.0.1", port, other, sendTrust, "Third", source("a", ByteArray(1)),
            PinPrompt { _, _ -> error("busy: no PIN expected") }, timeouts = timeouts).run()
        assertTrue(outcome.toString(), outcome is SendOutcome.Failed && outcome.reason.contains("busy"))
        hold.close()
    }

    @Test fun cancellingPairingModeWhileTheSenderTypesEndsItCleanly() {
        mode.start()
        val pinText = currentPin() // read it now: cancelling throws the PIN away
        val askedLatch = CountDownLatch(1)
        val release = CountDownLatch(1)
        var outcome: SendOutcome? = null
        val t = Thread {
            outcome = send(prompt = PinPrompt { _, _ -> askedLatch.countDown(); release.await(10, TimeUnit.SECONDS); pinText })
        }
        t.start()
        assertTrue(askedLatch.await(10, TimeUnit.SECONDS))
        mode.cancel()                         // the receiver's user cancels
        release.countDown()
        t.join(15000)
        val o = outcome
        assertTrue(o.toString(), o is SendOutcome.Failed)
        assertFalse(recvTrust.isTrusted(sendId.fingerprint))
    }

    @Test fun anImpostorBehindANearbyEntryIsCaughtByTheCertificate() {
        pairNow()
        val claimed = "ab".repeat(32) // the nearby list said this fingerprint, the certificate says otherwise
        val outcome = send(expected = claimed)
        assertTrue(outcome.toString(), outcome is SendOutcome.Failed && outcome.reason.contains("not the device it claims"))
        assertEquals("nothing was sent", listOf("pair.txt"), recvDir.list()!!.toList())
    }

    @Test fun correctExpectedFingerprintIsAccepted() {
        pairNow()
        assertEquals(SendOutcome.Sent("ok.txt"), send(name = "ok.txt", expected = recvId.fingerprint))
    }

    @Test fun fileFromAnUntrustedDeviceIsRefusedAtAnyMoment() {
        // directly, without hello
        raw().use { s ->
            s.say(message("file") { put("filename", "evil.txt"); put("size", 4) })
            val r = s.hear()
            assertEquals("file_result", r.getString("type"))
            assertEquals("not_paired", r.getString("code"))
        }
        // after hello, with pairing off
        raw().use { s ->
            s.say(message("hello") { put("name", "x"); put("known", true) })
            assertEquals("pairing_off", s.hear().getString("code"))
        }
        // in the middle of pairing
        mode.start()
        raw().use { s ->
            s.say(message("hello") { put("name", "x"); put("known", false) })
            assertEquals("pairing_required", s.hear().getString("status"))
            s.say(message("file") { put("filename", "evil.txt"); put("size", 4) })
            assertEquals("pairing_failed", s.hear().getString("code"))
        }
        assertTrue(recvDir.list().isNullOrEmpty())
    }

    @Test fun badFileNamesAreRefused() {
        pairNow()
        for (bad in listOf("../evil.txt", "a/b.txt", "/etc/passwd", "..", ".", "", "x\u0000y")) {
            raw().use { s ->
                s.say(message("hello") { put("name", "Test Sender"); put("known", true) })
                assertEquals("ok", s.hear().getString("status"))
                s.say(message("file") { put("filename", bad); put("size", 1) })
                val r = s.hear()
                assertEquals("error for '$bad'", "invalid_filename", r.optString("code"))
            }
        }
        assertEquals(listOf("pair.txt"), recvDir.list()!!.toList())
        assertFalse(File(recvDir.parentFile, "evil.txt").exists())
    }

    @Test fun badHeadersAreRefused() {
        pairNow()
        val cases = listOf(
            message("file") { put("filename", "a"); put("size", -1) } to "invalid_header",
            message("file") { put("filename", "a"); put("size", "5") } to "invalid_header",
            message("file") { put("filename", "a"); put("size", 1.5) } to "invalid_header",
            message("file") { put("filename", 7); put("size", 1) } to "invalid_header",
            message("file") { put("size", 1) } to "invalid_header",
            message("text") { put("body", "x") } to "unsupported",
            JSONObject().put("v", 1).put("type", "file").put("filename", "a").put("size", 1) to "unsupported",
        )
        for ((msg, code) in cases) {
            raw().use { s ->
                s.say(message("hello") { put("name", "Test Sender"); put("known", true) })
                assertEquals("ok", s.hear().getString("status"))
                s.say(msg)
                assertEquals(msg.toString(), code, s.hear().optString("code"))
            }
        }
    }

    @Test fun checksumMismatchLeavesNothingBehind() {
        pairNow()
        raw().use { s ->
            s.say(message("hello") { put("name", "Test Sender"); put("known", true) })
            assertEquals("ok", s.hear().getString("status"))
            s.say(message("file") { put("filename", "bad.bin"); put("size", 3) })
            s.outputStream.write(byteArrayOf(1, 2, 3))
            s.say(message("file_end") { put("sha256", "00".repeat(32)) })
            assertEquals("checksum_mismatch", s.hear().getString("code"))
        }
        Thread.sleep(200)
        assertEquals(listOf("pair.txt"), recvDir.list()!!.toList())
        assertTrue("a trusted device's failure is shown", recorder.failed.isNotEmpty())
    }

    @Test fun aTransferCutInTheMiddleLeavesNothingBehind() {
        pairNow()
        raw().use { s ->
            s.say(message("hello") { put("name", "Test Sender"); put("known", true) })
            s.hear()
            s.say(message("file") { put("filename", "cut.bin"); put("size", 1000) })
            s.outputStream.write(ByteArray(100))
            s.outputStream.flush()
            Thread.sleep(200)
        }
        Thread.sleep(500)
        assertEquals(listOf("pair.txt"), recvDir.list()!!.toList())
    }

    @Test fun malformedFramesAndVersionsAreRefused() {
        raw().use { s ->
            s.outputStream.write(byteArrayOf(0, 0, 0, 0)); s.outputStream.flush() // length 0
            assertEquals("invalid_header", s.hear().getString("code"))
        }
        raw().use { s ->
            s.say(JSONObject().put("v", 1).put("type", "hello").put("name", "old"))
            assertEquals("unsupported", s.hear().getString("code"))
        }
        raw().use { s ->
            s.say(message("hello") { put("known", true) }) // no name
            assertEquals("invalid_header", s.hear().getString("code"))
        }
        raw().use { s ->
            s.outputStream.write("not a frame at all......".toByteArray()); s.outputStream.flush()
            assertNotNull(s.hear().optString("code"))
        }
    }

    @Test fun silenceIsPunishedWithATimeout() {
        server.stop(); mode.cancel()
        startServer(Timeouts(helloMs = 300, idleMs = 300, firstPairMessageMs = 300, pairMessageMs = 300))
        raw().use { s ->
            val r = s.hear()
            assertEquals("hello_result", r.getString("type"))
            assertEquals("timeout", r.getString("code"))
        }
        // a stalled pairing
        mode.start()
        raw().use { s ->
            s.say(message("hello") { put("name", "x"); put("known", false) })
            s.hear()
            assertEquals("timeout", s.hear().getString("code")) // never sent the first pair message
        }
        assertTrue("silence before round 0 does not burn the PIN", mode.isActive())
    }

    @Test fun serverCapsConcurrentConnections() {
        val sockets = ArrayList<SSLSocket>()
        try {
            repeat(ReceiveServer.MAX_CONNECTIONS) { sockets.add(raw()) }
            // the next one is dropped quickly: the handshake or the first read fails
            val extra = try { raw() } catch (_: Exception) { null }
            if (extra != null) {
                extra.soTimeout = 3000
                extra.say(message("hello") { put("name", "x"); put("known", false) })
                val closed = try { extra.inputStream.read() < 0 } catch (_: Exception) { true }
                assertTrue(closed)
                extra.close()
            }
        } finally {
            sockets.forEach { it.close() }
        }
    }

    @Test fun peerWithoutAServerGetsAFriendlyMessage() {
        val outcome = SendSession("127.0.0.1", 1, sendId, sendTrust, "S", source("a", ByteArray(1)), PinPrompt { _, _ -> null },
            timeouts = Timeouts(connectMs = 1000)).run()
        assertTrue(outcome.toString(), outcome is SendOutcome.Failed && outcome.reason.contains("No answer"))
    }

    @Test fun plainTcpPeerIsNotAOneNodeDevice() {
        val dumb = java.net.ServerSocket(0)
        Thread { try { dumb.accept().use { it.getOutputStream().write("HTTP/1.0 200 OK\r\n\r\n".toByteArray()); Thread.sleep(500) } } catch (_: Exception) { } }.start()
        val outcome = SendSession("127.0.0.1", dumb.localPort, sendId, sendTrust, "S", source("a", ByteArray(1)), PinPrompt { _, _ -> null },
            timeouts = Timeouts(handshakeMs = 2000)).run()
        dumb.close()
        assertTrue(outcome.toString(), outcome is SendOutcome.Failed)
    }

    @Test fun cancelStopsASender() {
        mode.start()
        val asked = CountDownLatch(1)
        var outcome: SendOutcome? = null
        val session = SendSession("127.0.0.1", port, sendId, sendTrust, "S", source("a", ByteArray(1)),
            PinPrompt { _, _ -> asked.countDown(); Thread.sleep(5000); null }, timeouts = timeouts)
        val t = Thread { outcome = session.run() }
        t.start()
        assertTrue(asked.await(10, TimeUnit.SECONDS))
        session.cancel()
        t.join(10000)
        assertTrue(outcome.toString(), outcome is SendOutcome.Cancelled || outcome is SendOutcome.Failed)
    }
}
