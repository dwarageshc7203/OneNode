package app.onenode.core

import java.io.File
import java.nio.file.Files
import java.util.concurrent.Executors

// Helpers shared by the tests: real certificates (made with the JDK's own keytool, ECDSA P-256 like the app uses),
// temporary folders and a scheduler.
object TestSupport {
    init {
        // `gradle testConscrypt` runs everything with Android's TLS library in place of the JDK's
        if (System.getProperty("onenode.provider") == "conscrypt") {
            java.security.Security.insertProviderAt(org.conscrypt.Conscrypt.newProvider(), 1)
        }
    }

    private val cache = HashMap<String, Identity>()
    private val root: File by lazy { Files.createTempDirectory("onenode-test").toFile().also { it.deleteOnExit() } }

    @Synchronized
    fun identity(label: String): Identity = cache.getOrPut(label) {
        val file = File(root, "$label.p12")
        val keytool = File(System.getProperty("java.home"), "bin/keytool").path
        val process = ProcessBuilder(
            keytool, "-genkeypair", "-alias", label, "-keyalg", "EC", "-groupname", "secp256r1",
            "-sigalg", "SHA256withECDSA", "-dname", "CN=OneNode-$label", "-validity", "3650",
            "-storetype", "PKCS12", "-keystore", file.path, "-storepass", "changeit", "-keypass", "changeit",
        ).redirectErrorStream(true).start()
        val output = process.inputStream.readBytes().toString(Charsets.UTF_8)
        check(process.waitFor() == 0) { "keytool failed: $output" }
        Tls.identityFromPkcs12(file, "changeit".toCharArray())
    }

    fun tempDir(): File = Files.createTempDirectory(root.toPath(), "t").toFile()

    val scheduler by lazy { Executors.newScheduledThreadPool(1) { r -> Thread(r).apply { isDaemon = true } } }

    fun hex(c: Char, n: Int) = c.toString().repeat(n)

    // The other side finishes a moment after we do (it records the result after answering): wait for it, up to 5 seconds.
    fun eventually(message: String = "condition", check: () -> Boolean) {
        val end = System.nanoTime() + 5_000_000_000
        while (!check()) {
            if (System.nanoTime() > end) throw AssertionError("never became true: $message")
            Thread.sleep(20)
        }
    }
}
