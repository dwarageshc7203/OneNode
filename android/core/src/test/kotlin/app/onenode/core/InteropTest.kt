package app.onenode.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.net.ServerSocket
import java.util.concurrent.TimeUnit

// The real thing: this Kotlin code against the DESKTOP program (onenode-cli, built from the C++/Qt core).
// Both directions, first contact with a PIN, then again without one. Runs only when ONENODE_CLI points to the program:
//   ONENODE_CLI=/path/to/onenode-cli ./gradlew :core:test --tests '*InteropTest*'
class InteropTest {
    private val cli: String? = System.getenv("ONENODE_CLI")?.takeIf { it.isNotBlank() }
    private val identity = TestSupport.identity("android-sim")
    private val xdg = TestSupport.tempDir() // the desktop program keeps its identity and trusted list here for the whole test

    private fun freePort(): Int = ServerSocket(0).use { it.localPort }

    // Reads the program's output character by character: a prompt such as "Type the 6-digit PIN: " has no line break.
    private class Running(val process: Process) {
        val all = StringBuffer()
        private var from = 0
        init {
            for (stream in listOf(process.inputStream, process.errorStream)) {
                Thread {
                    val reader = stream.bufferedReader()
                    while (true) {
                        val c = reader.read()
                        if (c < 0) break
                        all.append(c.toChar())
                    }
                }.apply { isDaemon = true }.start()
            }
        }
        // waits until `text` appears in the output (after the previous match); returns the rest of that line
        fun waitFor(text: String, seconds: Long = 20): String {
            val end = System.nanoTime() + seconds * 1_000_000_000
            while (System.nanoTime() < end) {
                val i = all.indexOf(text, from)
                if (i >= 0) {
                    val eol = all.indexOf("\n", i).let { if (it < 0) all.length else it }
                    from = i + text.length
                    return all.substring(i, eol)
                }
                Thread.sleep(50)
            }
            throw AssertionError("never saw '$text'. Output so far:\n$all")
        }
    }

    private fun start(vararg args: String): Running {
        val pb = ProcessBuilder(listOf(cli!!) + args)
        pb.environment()["XDG_DATA_HOME"] = xdg.path
        pb.environment()["HOME"] = xdg.path
        pb.environment()["QT_QPA_PLATFORM"] = "offscreen"
        return Running(pb.start())
    }

    @Test fun androidSendsToDesktop() {
        assumeTrue("ONENODE_CLI not set: interop test skipped", cli != null)
        val port = freePort()
        val dest = TestSupport.tempDir()
        val desktop = start("receive", "--port", port.toString(), "--dest", dest.path, "--pair")
        try {
            desktop.waitFor("Listening on port")
            val pinLine = desktop.waitFor("PIN")
            val pin = Regex("PIN ([0-9 ]+)").find(pinLine)!!.groupValues[1].trim()

            val trust = TrustStore(File(TestSupport.tempDir(), "t.json")).also { it.load() }
            val payload = ByteArray(2_000_000) { (it * 31).toByte() }
            val outcome = SendSession("127.0.0.1", port, identity, trust, "Kotlin Phone",
                FileSource("from-kotlin.bin", payload.size.toLong()) { ByteArrayInputStream(payload) },
                PinPrompt { _, _ -> pin }, timeouts = Timeouts()).run()
            assertEquals(SendOutcome.Sent("from-kotlin.bin"), outcome)
            assertTrue(File(dest, "from-kotlin.bin").readBytes().contentEquals(payload))
            assertEquals(1, trust.devices().size)

            // now both know each other: no PIN
            val second = SendSession("127.0.0.1", port, identity, trust, "Kotlin Phone",
                FileSource("second.txt", 5) { ByteArrayInputStream("hello".toByteArray()) },
                PinPrompt { _, _ -> error("no PIN expected") }).run()
            assertEquals(SendOutcome.Sent("second.txt"), second)
            assertEquals("hello", File(dest, "second.txt").readText())
        } finally {
            desktop.process.destroy()
        }
    }

    @Test fun desktopSendsToAndroid() {
        assumeTrue("ONENODE_CLI not set: interop test skipped", cli != null)
        val dest = TestSupport.tempDir()
        val trust = TrustStore(File(TestSupport.tempDir(), "t.json")).also { it.load() }
        val mode = PairingMode(TestSupport.scheduler)
        val server = ReceiveServer(0, ReceiveServices(identity, { "Kotlin Phone" }, trust, mode, DirectorySink(dest)), object : ReceiveEvents {})
        val port = server.start()
        try {
            val file = File(TestSupport.tempDir(), "from-desktop.bin").also { f -> f.writeBytes(ByteArray(1_500_000) { (it * 7).toByte() }) }

            // first contact: desktop asks for the PIN on stdin
            mode.start()
            val first = start("send", "127.0.0.1", file.path, "--port", port.toString())
            first.waitFor("Type the 6-digit PIN")
            first.process.outputStream.write((PairingEngine.formatPin(mode.pin()) + "\n").toByteArray())
            first.process.outputStream.flush()
            first.waitFor("Sent:")
            assertTrue(first.process.waitFor(20, TimeUnit.SECONDS))
            assertTrue(File(dest, "from-desktop.bin").readBytes().contentEquals(file.readBytes()))
            assertEquals(1, trust.devices().size)

            // again: no PIN
            val again = start("send", "127.0.0.1", file.path, "--port", port.toString())
            again.waitFor("Sent:")
            assertTrue(File(dest, "from-desktop(1).bin").exists())
        } finally {
            server.stop()
        }
    }

    @Test fun desktopWithPairingOffRefusesUs() {
        assumeTrue("ONENODE_CLI not set: interop test skipped", cli != null)
        val port = freePort()
        val desktop = start("receive", "--port", port.toString(), "--dest", TestSupport.tempDir().path)
        try {
            desktop.waitFor("Listening on port")
            val trust = TrustStore(File(TestSupport.tempDir(), "t.json")).also { it.load() }
            val outcome = SendSession("127.0.0.1", port, identity, trust, "Kotlin Phone",
                FileSource("x", 1) { ByteArrayInputStream(ByteArray(1)) }, PinPrompt { _, _ -> error("no PIN") }).run()
            assertTrue(outcome.toString(), outcome is SendOutcome.Failed && outcome.reason.contains("Pair a new device"))
        } finally {
            desktop.process.destroy()
        }
    }
}
