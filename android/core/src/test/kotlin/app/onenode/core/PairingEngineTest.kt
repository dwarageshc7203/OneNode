package app.onenode.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PairingEngineTest {
    private val fpS = ByteArray(32) { 0x11 }
    private val fpR = ByteArray(32) { 0x22 }

    private fun senderNonce(i: Int) = ByteArray(16) { i.toByte() }
    private fun receiverNonce(i: Int) = ByteArray(16) { (0x80 + i).toByte() }

    private fun pair(pinS: Int, pinR: Int, fpSR: ByteArray = fpR, fpRS: ByteArray = fpS): Pair<PairingEngine, PairingEngine> {
        val s = PairingEngine(PairingEngine.Role.SENDER, pinS, fpS, fpSR, ::senderNonce)
        val r = PairingEngine(PairingEngine.Role.RECEIVER, pinR, fpRS, fpR, ::receiverNonce)
        return s to r
    }

    // The test vector of PROTOCOL.md 5.5. Same numbers as the desktop tests, so both implementations agree.
    @Test fun matchesTheDesktopTestVector() {
        val s = PairingEngine(PairingEngine.Role.SENDER, 482913, fpS, fpR, ::senderNonce)
        val r = PairingEngine(PairingEngine.Role.RECEIVER, 482913, fpS, fpR, ::receiverNonce)

        val commitsS = HashMap<Int, String>()
        val commitsR = HashMap<Int, String>()
        for (round in 0 until 20) {
            val cs = s.makeMessage()
            commitsS[round] = cs.getString("commit")
            assertTrue(r.accept(cs))
            val cr = r.makeMessage()
            commitsR[round] = cr.getString("commit")
            assertTrue(s.accept(cr))
            val os = s.makeMessage()
            assertTrue(r.accept(os))
            val or = r.makeMessage()
            assertTrue(s.accept(or))
        }
        assertEquals(PairingEngine.Action.DONE, s.action())
        assertEquals(PairingEngine.Action.DONE, r.action())

        assertEquals("04c1fd647242b4d7369c627a7f2e1931432e9a8dcd1d4012f0b70aefed833bf0", commitsS[0])
        assertEquals("9e00bdc6f21e1212c88a9ef586e27c08e2e31985b9a7e5a2e71a8ee995fca376", commitsS[1])
        assertEquals("4554b6c5592979e5e9ab1678ceb60064c945f2f2d9ade66b981a57f03861114c", commitsS[19])
        assertEquals("cdd47beb0cabc581177bae31264da90c001191aff0bf363b73e6fe5e38a9366a", commitsR[0])
        assertEquals("e0638040a1d70cb798fe3d2e6080e154d38f9a289483a3d7c4179554167e6895", commitsR[1])
        assertEquals("1d79bf59f8f49c6f264523393b0d7c10aa05776480970afdee29dc3b05d018cd", commitsR[19])
    }

    // runs the whole exchange in memory; returns true when both finished
    private fun run(s: PairingEngine, r: PairingEngine): Boolean {
        repeat(20) {
            if (!r.accept(s.makeMessage())) return false
            if (!s.accept(r.makeMessage())) return false
            if (!r.accept(s.makeMessage())) return false
            if (!s.accept(r.makeMessage())) return false
        }
        return s.action() == PairingEngine.Action.DONE && r.action() == PairingEngine.Action.DONE
    }

    @Test fun samePinPairs() {
        val (s, r) = pair(123456, 123456)
        assertTrue(run(s, r))
    }

    @Test fun randomNoncesPairToo() {
        val s = PairingEngine(PairingEngine.Role.SENDER, 7, fpS, fpR)
        val r = PairingEngine(PairingEngine.Role.RECEIVER, 7, fpS, fpR)
        assertTrue(run(s, r))
    }

    @Test fun wrongPinNeverGetsThrough() {
        for (wrong in listOf(123457, 0, 999999, 123456 xor 1, 123456 xor (1 shl 19))) {
            val (s, r) = pair(123456, wrong)
            assertFalse("pin $wrong", run(s, r))
        }
    }

    @Test fun manyWrongPinsAllFail() {
        var passed = 0
        for (i in 0 until 2000) {
            val s = PairingEngine(PairingEngine.Role.SENDER, i, fpS, fpR)
            val r = PairingEngine(PairingEngine.Role.RECEIVER, (i * 7919 + 13) % 1_000_000, fpS, fpR)
            if (i != (i * 7919 + 13) % 1_000_000 && run(s, r)) passed++
        }
        assertEquals(0, passed)
    }

    // a device in the middle shows each side its own certificate: the two sides see different fingerprints
    @Test fun differentFingerprintsFailEvenWithTheRightPin() {
        val other = ByteArray(32) { 0x33 }
        val (s, r) = pair(555555, 555555, fpSR = other) // the sender believes the receiver is "other"
        assertFalse(run(s, r))
    }

    @Test fun outOfOrderAndWrongRoundFail() {
        val (s, r) = pair(1, 1)
        val c0 = s.makeMessage()
        // the receiver expects a commit; an "open" is out of order
        val o = org.json.JSONObject().put("v", 2).put("type", "pair_open").put("round", 0).put("nonce", "00".repeat(16))
        assertFalse(r.accept(o))
        assertEquals(PairingEngine.Action.FAILED, r.action())

        val (s2, r2) = pair(1, 1)
        val c = s2.makeMessage().put("round", 1)
        assertFalse(r2.accept(c))
        assertTrue(c0.getString("type") == "pair_commit" && s.action() == PairingEngine.Action.EXPECT_COMMIT)
    }

    @Test fun malformedFieldsFail() {
        for (bad in listOf(
            org.json.JSONObject().put("v", 2).put("type", "pair_commit").put("round", 0).put("commit", "ABCD"),
            org.json.JSONObject().put("v", 2).put("type", "pair_commit").put("round", 0).put("commit", "AB".repeat(32)),
            org.json.JSONObject().put("v", 2).put("type", "pair_commit").put("round", "0").put("commit", "ab".repeat(32)),
            org.json.JSONObject().put("v", 1).put("type", "pair_commit").put("round", 0).put("commit", "ab".repeat(32)),
            org.json.JSONObject().put("type", "pair_commit").put("round", 0).put("commit", "ab".repeat(32)),
        )) {
            val r = PairingEngine(PairingEngine.Role.RECEIVER, 1, fpS, fpR)
            assertFalse(bad.toString(), r.accept(bad))
        }
    }

    @Test fun invalidSetupStartsFailed() {
        assertEquals(PairingEngine.Action.FAILED, PairingEngine(PairingEngine.Role.SENDER, -1, fpS, fpR).action())
        assertEquals(PairingEngine.Action.FAILED, PairingEngine(PairingEngine.Role.SENDER, 1_000_000, fpS, fpR).action())
        assertEquals(PairingEngine.Action.FAILED, PairingEngine(PairingEngine.Role.SENDER, 1, ByteArray(5), fpR).action())
    }

    @Test fun pinHelpers() {
        assertEquals("000 042", PairingEngine.formatPin(42))
        assertEquals("482 913", PairingEngine.formatPin(482913))
        assertEquals(482913, PairingEngine.parsePin("482913"))
        assertEquals(482913, PairingEngine.parsePin("482 913"))
        assertEquals(42, PairingEngine.parsePin("000042"))
        for (bad in listOf("", "12345", "1234567", "12a456", "-12345", "12-456")) assertNull(bad, PairingEngine.parsePin(bad))
        repeat(1000) { assertTrue(PairingEngine.generatePin() in 0..999_999) }
    }
}
