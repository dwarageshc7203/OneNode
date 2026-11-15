package app.onenode.core

// Progress is reported at most about 10 times a second (plus always at the very end), so a fast transfer
// does not flood the screen with updates.
class ProgressThrottle(private val minIntervalMs: Long = 100, private val clock: () -> Long = System::nanoTime) {
    private var last = 0L
    private var first = true

    fun shouldEmit(done: Long, total: Long): Boolean {
        val now = clock()
        if (first || done >= total || (now - last) / 1_000_000 >= minIntervalMs) {
            first = false
            last = now
            return true
        }
        return false
    }
}
