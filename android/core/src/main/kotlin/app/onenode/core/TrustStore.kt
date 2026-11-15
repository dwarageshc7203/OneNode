package app.onenode.core

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.time.Instant

// The list of paired devices (PROTOCOL.md section 10): fingerprint + name + time. Same file format as the desktop app.
// A damaged or foreign file is never overwritten: the list stays empty and read-only until the user deals with it.
class TrustStore(private val file: File, private val onChange: () -> Unit = {}) {
    data class Device(val fingerprint: String, val name: String, val pairedAt: String)

    private val lock = Any()
    private var list: List<Device> = emptyList()
    private var readOnly = false

    // Returns null when all is well, otherwise a sentence for the user.
    fun load(): String? = synchronized(lock) {
        list = emptyList()
        readOnly = false
        if (!file.exists()) return null // first start: nobody is trusted yet

        readOnly = true // from here on any problem keeps the list empty and refuses to write
        val root = try {
            JSONObject(file.readText(Charsets.UTF_8))
        } catch (e: IOException) {
            return "Could not read the list of paired devices."
        } catch (e: JSONException) {
            return "The list of paired devices is damaged. It was not changed."
        }
        if (root.optInt("schemaVersion", -1) != 1) {
            return "The list of paired devices was made by a different version of OneNode. It was not changed."
        }
        val loaded = ArrayList<Device>()
        val entries = root.optJSONArray("devices") ?: JSONArray()
        for (i in 0 until entries.length()) {
            val entry = entries.optJSONObject(i) ?: return "The list of paired devices is damaged. It was not changed."
            val fp = entry.optString("fingerprint", "")
            if (!Names.isValidFingerprint(fp)) return "The list of paired devices is damaged. It was not changed."
            loaded.add(Device(fp, Names.cleanName(entry.optString("name", "")), entry.optString("pairedAt", "")))
        }
        list = loaded
        readOnly = false
        null
    }

    fun isTrusted(fingerprint: String): Boolean = synchronized(lock) { list.any { it.fingerprint == fingerprint.lowercase() } }

    fun nameOf(fingerprint: String): String? = synchronized(lock) { list.firstOrNull { it.fingerprint == fingerprint.lowercase() }?.name }

    fun devices(): List<Device> = synchronized(lock) { list }

    // null = saved. Otherwise nothing changed and the text says why.
    fun add(fingerprint: String, name: String): String? {
        val problem = synchronized(lock) {
            if (readOnly) return@synchronized "The list of paired devices could not be read, so nothing can be added to it."
            val wanted = fingerprint.lowercase()
            if (!Names.isValidFingerprint(wanted)) return@synchronized "Not a valid device fingerprint."
            val before = list
            val clean = Names.cleanName(name)
            list = if (before.any { it.fingerprint == wanted }) {
                before.map { if (it.fingerprint == wanted) it.copy(name = clean) else it } // renamed: keep ONE entry and its first pairing time
            } else {
                before + Device(wanted, clean, Instant.now().toString())
            }
            val saved = save()
            if (saved != null) list = before // not saved, so not trusted either
            saved
        }
        if (problem == null) onChange()
        return problem
    }

    fun remove(fingerprint: String): String? {
        val problem = synchronized(lock) {
            if (readOnly) return@synchronized "The list of paired devices could not be read, so nothing can be removed from it."
            val wanted = fingerprint.lowercase()
            val before = list
            list = before.filterNot { it.fingerprint == wanted }
            val saved = save()
            if (saved != null) list = before
            saved
        }
        if (problem == null) onChange()
        return problem
    }

    // write a temporary file, then rename it over the real one: a crash never leaves half a list
    private fun save(): String? {
        return try {
            val root = JSONObject().put("schemaVersion", 1)
            val entries = JSONArray()
            for (d in list) {
                entries.put(JSONObject().put("fingerprint", d.fingerprint).put("name", d.name).put("pairedAt", d.pairedAt))
            }
            root.put("devices", entries)
            file.absoluteFile.parentFile?.mkdirs()
            val tmp = File(file.path + ".tmp")
            tmp.writeText(root.toString(2), Charsets.UTF_8)
            try {
                Files.move(tmp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
            } catch (e: AtomicMoveNotSupportedException) {
                Files.move(tmp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING)
            }
            null
        } catch (e: IOException) {
            "Could not save the list of paired devices: ${e.message}"
        }
    }
}
