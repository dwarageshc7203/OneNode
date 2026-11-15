package app.onenode

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import app.onenode.core.Identity
import java.math.BigInteger
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.cert.X509Certificate
import java.security.spec.ECGenParameterSpec
import java.util.Date
import javax.security.auth.x500.X500Principal

// This phone's "ID card". The key pair is created INSIDE the Android Keystore (hardware-backed where the phone has it):
// the private key can never be read out or copied, not even by this app. The Keystore also creates the self-signed
// certificate for it. The certificate's SHA-256 is the device's fingerprint.
object AndroidIdentity {
    private const val ALIAS = "onenode-identity-v1"
    private const val THIRTY_YEARS_MS = 30L * 365 * 24 * 3600 * 1000

    // Returns the identity, creating it on the first start. If the key was lost (a restored phone, a reset Keystore)
    // a new one is made: the other devices then see a new device and pairing is needed again.
    fun load(): Identity {
        read()?.let { return it }
        create()
        return read() ?: error("The Android Keystore did not keep the new device key.")
    }

    private fun read(): Identity? {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        return try {
            val key = store.getKey(ALIAS, null) as? PrivateKey ?: return null
            val chain = store.getCertificateChain(ALIAS)?.map { it as X509Certificate }?.toTypedArray()
            if (chain == null || chain.isEmpty()) null else Identity(key, chain)
        } catch (e: java.security.GeneralSecurityException) {
            null // unusable key: treated as lost
        }
    }

    private fun create() {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        if (store.containsAlias(ALIAS)) store.deleteEntry(ALIAS)

        val now = System.currentTimeMillis()
        val spec = KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN)
            .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
            // TLS 1.3 signs with ECDSA + SHA-256; some TLS stacks hand the Keystore an already-hashed value (NONE)
            .setDigests(KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_NONE)
            .setCertificateSubject(X500Principal("CN=OneNode"))
            .setCertificateSerialNumber(BigInteger.valueOf(now))
            .setCertificateNotBefore(Date(now - 24 * 3600 * 1000))
            .setCertificateNotAfter(Date(now + THIRTY_YEARS_MS)) // the date does not matter to OneNode (it trusts fingerprints)
            .build()
        KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore").apply {
            initialize(spec)
            generateKeyPair()
        }
    }
}
