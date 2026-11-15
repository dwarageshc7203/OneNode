package app.onenode

import android.content.ContentResolver
import android.content.ContentValues
import android.net.Uri
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import app.onenode.core.FileSink
import app.onenode.core.Names
import java.io.IOException
import java.io.OutputStream
import java.util.Locale

// Received files land in  Downloads/OneNode  through MediaStore. That needs no storage permission, and Android itself
// refuses to overwrite: a name that already exists becomes "name (1).ext". While a file is arriving it is marked
// "pending", so no other app (and no gallery) sees a half-written file; it appears when the file is complete and verified.
class MediaStoreSink(private val resolver: ContentResolver) : FileSink {
    override fun create(name: String, size: Long): FileSink.Incoming {
        val safe = Names.safeFileName(name)
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, safe)
            put(MediaStore.MediaColumns.MIME_TYPE, mimeFor(safe))
            put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/OneNode")
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
            ?: throw IOException("Android would not create the file in Downloads.")
        val stream = try {
            resolver.openOutputStream(uri) ?: throw IOException("Android would not open the new file.")
        } catch (e: Exception) {
            resolver.delete(uri, null, null)
            throw if (e is IOException) e else IOException(e.message, e)
        }
        return Incoming(resolver, uri, stream)
    }

    private fun mimeFor(name: String): String {
        val ext = name.substringAfterLast('.', "").lowercase(Locale.ROOT)
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
    }

    private class Incoming(
        private val resolver: ContentResolver,
        private val uri: Uri,
        override val out: OutputStream,
    ) : FileSink.Incoming {
        private var done = false

        override fun commit(): String {
            check(!done)
            done = true
            out.close()
            val values = ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }
            resolver.update(uri, values, null, null)
            // Android may have changed the name ("name (1).ext"): report the name the file really got
            resolver.query(uri, arrayOf(MediaStore.MediaColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) return it.getString(0)
            }
            return "file"
        }

        override fun abort() {
            if (done) return
            done = true
            try { out.close() } catch (_: IOException) { }
            try { resolver.delete(uri, null, null) } catch (_: RuntimeException) { }
        }
    }
}
