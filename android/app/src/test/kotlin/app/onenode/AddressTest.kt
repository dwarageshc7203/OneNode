package app.onenode

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class AddressTest {
    @Test fun plainAddresses() {
        assertEquals(HostPort("192.168.1.20", 45454), Address.parse("192.168.1.20"))
        assertEquals(HostPort("192.168.1.20", 5000), Address.parse(" 192.168.1.20:5000 "))
        assertEquals(HostPort("laptop.local", 45454), Address.parse("laptop.local"))
        assertEquals(HostPort("my-pc", 1), Address.parse("my-pc:1"))
    }

    @Test fun ipv6() {
        assertEquals(HostPort("fe80::1", 45454), Address.parse("[fe80::1]"))
        assertEquals(HostPort("fe80::1", 9), Address.parse("[fe80::1]:9"))
        assertEquals(HostPort("fe80::1", 45454), Address.parse("fe80::1"))
    }

    @Test fun rejectsNonsense() {
        for (bad in listOf("", "  ", "a b", "192.168.1.20:0", "192.168.1.20:70000", "192.168.1.20:", "host:abc", "-bad", "bad-", "[::1", "[::1]x", "http://x", "a/b", "x".repeat(300))) {
            assertNull("'$bad'", Address.parse(bad))
        }
    }
}
