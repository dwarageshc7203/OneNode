package app.onenode

import android.content.ContentResolver
import android.net.Uri
import android.provider.OpenableColumns
import app.onenode.core.FileSource
import java.io.File
import java.io.IOException
import java.util.UUID

// Turns a file the user picked (a content:// address) into something the core can send: name, size, bytes.
object Sources {
    class Described(val name: String, val size: Long)

    fun describe(resolver: ContentResolver, uri: Uri): Described {
        var name: String? = null
        var size = -1L
        try {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    c.getColumnIndex(OpenableColumns.DISPLAY_NAME).takeIf { it >= 0 }?.let { name = c.getString(it) }
                    c.getColumnIndex(OpenableColumns.SIZE).takeIf { it >= 0 && !c.isNull(it) }?.let { size = c.getLong(it) }
                }
            }
        } catch (_: RuntimeException) {
            // some providers refuse the query: fall through to the other ways
        }
        if (size < 0) {
            size = try {
                resolver.openAssetFileDescriptor(uri, "r")?.use { it.length } ?: -1L
            } catch (_: Exception) {
                -1L
            }
        }
        val safeName = name?.takeIf { it.isNotBlank() } ?: uri.lastPathSegment?.substringAfterLast('/') ?: "file"
        return Described(safeName, size)
    }

    // Some sources (a cloud file that is generated on the fly) cannot tell their size in advance, and OneNode must say
    // the size first. Those are copied once into the app's cache, which then gives the size.
    fun open(resolver: ContentResolver, uri: Uri, cacheDir: File): FileSource {
        val info = describe(resolver, uri)
        if (info.size >= 0) {
            return FileSource(info.name, info.size) { resolver.openInputStream(uri) ?: throw IOException("The file could not be opened.") }
        }
        val dir = File(cacheDir, "outgoing").apply { mkdirs() }
        val copy = File(dir, UUID.randomUUID().toString())
        try {
            (resolver.openInputStream(uri) ?: throw IOException("The file could not be opened.")).use { input ->
                copy.outputStream().use { input.copyTo(it) }
            }
        } catch (e: IOException) {
            copy.delete()
            throw e
        }
        return FileSource(info.name, copy.length()) {
            object : java.io.FilterInputStream(copy.inputStream()) {
                override fun close() { super.close(); copy.delete() }
            }
        }
    }

    fun clearCache(cacheDir: File) {
        File(cacheDir, "outgoing").deleteRecursively()
    }
}
