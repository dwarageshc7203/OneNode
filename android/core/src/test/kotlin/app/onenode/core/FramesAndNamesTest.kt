package app.onenode.core

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.EOFException
import java.nio.ByteBuffer

class FramesAndNamesTest {
    private fun frame(json: String): ByteArray {
        val body = json.toByteArray()
        return ByteBuffer.allocate(4 + body.size).putInt(body.size).put(body).array()
    }

    @Test fun roundTrip() {
        val msg = message("hello") { put("name", "Phone"); put("known", true) }
        val back = Frames.read(ByteArrayInputStream(Frames.encode(msg)))
        assertEquals("hello", back.getString("type"))
        assertEquals(2, back.getInt("v"))
        assertEquals("Phone", back.getString("name"))
    }

    @Test fun lengthPrefixIsBigEndianAndCountsOnlyJson() {
        val bytes = Frames.encode(JSONObject().put("v", 2).put("type", "x"))
        val n = ByteBuffer.wrap(bytes, 0, 4).int
        assertEquals(bytes.size - 4, n)
    }

    @Test fun rejectsBadLengths() {
        for (len in intArrayOf(0, 16385, -1, Int.MAX_VALUE)) {
            try {
                Frames.read(ByteArrayInputStream(ByteBuffer.allocate(8).putInt(len).putInt(0).array()))
                fail("length $len accepted")
            } catch (e: ProtocolException) {
                assertEquals("invalid_header", e.code)
            }
        }
    }

    @Test fun acceptsMaximumLength() {
        val pad = "a".repeat(16384 - """{"p":""}""".length)
        val raw = frame("""{"p":"$pad"}""")
        assertEquals(16384, raw.size - 4)
        assertEquals(pad.length, Frames.read(ByteArrayInputStream(raw)).getString("p").length)
    }

    @Test fun rejectsNonJsonAndNonObjects() {
        for (text in listOf("not json", "[1,2]", "42", "\"x\"", "{")) {
            try {
                Frames.read(ByteArrayInputStream(frame(text)))
                fail("accepted: $text")
            } catch (e: ProtocolException) {
                assertEquals("invalid_header", e.code)
            }
        }
    }

    @Test fun truncatedFrameIsEof() {
        val raw = frame("""{"v":2,"type":"x"}""")
        try {
            Frames.read(ByteArrayInputStream(raw.copyOf(raw.size - 3)))
            fail()
        } catch (_: EOFException) {
        }
    }

    @Test fun twoFramesInARowAreReadOneByOne() {
        val both = frame("""{"v":2,"type":"a"}""") + frame("""{"v":2,"type":"b"}""")
        val input = ByteArrayInputStream(both)
        assertEquals("a", Frames.read(input).getString("type"))
        assertEquals("b", Frames.read(input).getString("type"))
    }

    @Test fun strictReadersDoNotConvertTypes() {
        val o = JSONObject("""{"s":"2","n":2,"d":2.5,"e":2.0,"b":true,"t":"true"}""")
        assertNull(o.strictLong("s"))
        assertEquals(2L, o.strictLong("n"))
        assertNull(o.strictLong("d"))
        assertEquals(2L, o.strictLong("e"))
        assertNull(o.strictString("n"))
        assertEquals(true, o.strictBool("b"))
        assertNull(o.strictBool("t"))
    }

    // ---- names ----

    @Test fun cleanNameBasics() {
        assertEquals("Dwarag Phone", Names.cleanName("  Dwarag \t\n Phone  "))
        assertEquals("AB", Names.cleanName("A\u0000\u0007‮B"))   // control and bidi-format characters go
        assertEquals(Names.UNKNOWN, Names.cleanName(""))
        assertEquals(Names.UNKNOWN, Names.cleanName("\u0000 ​"))
        assertEquals(64, Names.cleanName("x".repeat(200)).length)
        assertEquals("héllo ✓", Names.cleanName("héllo ✓"))
    }

    @Test fun cleanNameNeverCutsACharacterInHalf() {
        val name = Names.cleanName("a".repeat(63) + "😀😀")
        assertTrue(name.all { !Character.isSurrogate(it) } || name.codePointCount(0, name.length) * 2 >= name.length - 2)
        assertTrue(name.length <= 64)
        assertTrue(!Character.isHighSurrogate(name.last()))
    }

    @Test fun fingerprints() {
        assertTrue(Names.isValidFingerprint("ab".repeat(32)))
        assertTrue(!Names.isValidFingerprint("AB".repeat(32)))
        assertTrue(!Names.isValidFingerprint("ab".repeat(31)))
        assertEquals("00ff10", Names.toHex(byteArrayOf(0, -1, 16)))
        assertEquals(listOf<Byte>(0, -1, 16), Names.fromHex("00ff10").toList())
    }

    @Test fun safeFileName() {
        assertEquals("a_b_c.txt", Names.safeFileName("a\u0001b\\c.txt"))
        assertEquals("file", Names.safeFileName("   "))
        assertEquals("file", Names.safeFileName(".."))
        val long = Names.safeFileName("x".repeat(500) + ".mp4")
        assertTrue(long.length <= 200 && long.endsWith(".mp4"))
    }
}
