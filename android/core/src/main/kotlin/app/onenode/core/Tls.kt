package app.onenode.core

import java.io.File
import java.io.IOException
import java.net.Socket
import java.security.KeyStore
import java.security.Principal
import java.security.PrivateKey
import java.security.cert.X509Certificate
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLEngine
import javax.net.ssl.SSLSocket
import javax.net.ssl.X509ExtendedKeyManager
import javax.net.ssl.X509ExtendedTrustManager

// A device's "ID card": a self-signed certificate and its private key.
// On Android the key lives inside the AndroidKeyStore (it can never be read out); on a computer it comes from a file.
class Identity(val privateKey: PrivateKey, val chain: Array<X509Certificate>) {
    val fingerprint: String = Tls.fingerprintOf(chain[0])
}

// TLS 1.3 only, both sides show a certificate, and NOBODY is trusted by certificate chain: every certificate is let
// through the handshake and the application then decides by FINGERPRINT (PROTOCOL.md section 1).
object Tls {
    private const val ALIAS = "onenode"
    val PROTOCOLS = arrayOf("TLSv1.3")

    fun fingerprintOf(cert: X509Certificate): String = Names.toHex(Names.sha256(cert.encoded))

    // The fingerprint of the certificate the other side showed in the handshake (never anything it SAYS).
    fun peerFingerprint(socket: SSLSocket): String? = try {
        val chain = socket.session.peerCertificates
        (chain.firstOrNull() as? X509Certificate)?.let { fingerprintOf(it) }
    } catch (e: IOException) {
        null // javax.net.ssl.SSLPeerUnverifiedException: no certificate
    }

    fun context(identity: Identity): SSLContext {
        val context = SSLContext.getInstance("TLS")
        context.init(arrayOf(FixedKeyManager(identity)), arrayOf(AcceptAnyCertificate), null)
        return context
    }

    // Applies "TLS 1.3 only" to a socket; throws a readable error if this system has no TLS 1.3.
    fun restrict(socket: SSLSocket) {
        if (PROTOCOLS.any { it !in socket.supportedProtocols }) {
            throw IOException("This system does not support TLS 1.3.")
        }
        socket.enabledProtocols = PROTOCOLS
    }

    // For computers and tests: an identity from a PKCS#12 file (for example made with keytool).
    fun identityFromPkcs12(file: File, password: CharArray): Identity {
        val store = KeyStore.getInstance("PKCS12")
        file.inputStream().use { store.load(it, password) }
        val alias = store.aliases().toList().first()
        val key = store.getKey(alias, password) as PrivateKey
        val chain = store.getCertificateChain(alias).map { it as X509Certificate }.toTypedArray()
        return Identity(key, chain)
    }

    // Offers our one certificate whatever the other side asks for (a self-signed certificate matches no "issuer" list).
    private class FixedKeyManager(private val identity: Identity) : X509ExtendedKeyManager() {
        override fun getClientAliases(keyType: String?, issuers: Array<out Principal>?) = arrayOf(ALIAS)
        override fun chooseClientAlias(keyType: Array<out String>?, issuers: Array<out Principal>?, socket: Socket?) = ALIAS
        override fun chooseEngineClientAlias(keyType: Array<out String>?, issuers: Array<out Principal>?, engine: SSLEngine?) = ALIAS
        override fun getServerAliases(keyType: String?, issuers: Array<out Principal>?) = arrayOf(ALIAS)
        override fun chooseServerAlias(keyType: String?, issuers: Array<out Principal>?, socket: Socket?) = ALIAS
        override fun chooseEngineServerAlias(keyType: String?, issuers: Array<out Principal>?, engine: SSLEngine?) = ALIAS
        override fun getCertificateChain(alias: String?) = if (alias == ALIAS) identity.chain else null
        override fun getPrivateKey(alias: String?) = if (alias == ALIAS) identity.privateKey else null
    }

    // Lets every certificate through the handshake. This is NOT "trust everything": nothing is sent to or accepted
    // from a device before the fingerprint of its certificate has been checked against the trusted-devices list
    // (or, for pairing, by the PIN exchange, whose commitments contain both fingerprints).
    private object AcceptAnyCertificate : X509ExtendedTrustManager() {
        override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) = Unit
        override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) = Unit
        override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?, socket: Socket?) = Unit
        override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?, socket: Socket?) = Unit
        override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?, engine: SSLEngine?) = Unit
        override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?, engine: SSLEngine?) = Unit
        override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
    }
}
