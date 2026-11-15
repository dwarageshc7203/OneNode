package app.onenode.core

import java.io.Closeable
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.StandardOpenOption

// Where received files go. The core does not know about folders or Android's MediaStore: it asks a sink.
// Rule for every sink: NEVER overwrite an existing file (pick a new name instead) and show nothing under the final
// name until the whole file has arrived and been verified.
interface FileSink {
    @Throws(IOException::class)
    fun create(name: String, size: Long): Incoming

    interface Incoming : Closeable {
        val out: OutputStream
        fun commit(): String // the file is complete and verified: give it its final name; returns the name it got
        fun abort()          // the transfer failed: remove everything
        override fun close() = abort()
    }
}

// A sink writing into a normal folder (used by tests and by computers).
class DirectorySink(private val dir: File) : FileSink {
    override fun create(name: String, size: Long): FileSink.Incoming {
        dir.mkdirs()
        val safe = Names.safeFileName(name)
        var attempt = 0
        while (attempt < 1000) {
            val candidate = File(dir, withNumber(safe, attempt))
            val part = File(dir, candidate.name + Protocol.PART_SUFFIX)
            if (!candidate.exists() && !part.exists()) {
                try {
                    val stream = Files.newOutputStream(part.toPath(), StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)
                    return Incoming(candidate, part, stream)
                } catch (e: FileAlreadyExistsException) {
                    // somebody took it a moment ago: try the next number
                }
            }
            attempt++
        }
        throw IOException("No free file name for $safe")
    }

    private fun withNumber(name: String, n: Int): String {
        if (n == 0) return name
        val dot = name.lastIndexOf('.')
        return if (dot > 0) name.substring(0, dot) + "($n)" + name.substring(dot) else "$name($n)"
    }

    private class Incoming(private val target: File, private val part: File, override val out: OutputStream) : FileSink.Incoming {
        private var done = false

        override fun commit(): String {
            check(!done)
            done = true
            out.close()
            // moving WITHOUT "replace": if the name was taken in the meantime this fails instead of destroying a file
            Files.move(part.toPath(), target.toPath())
            return target.name
        }

        override fun abort() {
            if (done) return
            done = true
            try { out.close() } catch (_: IOException) { }
            part.delete()
        }
    }
}
