package com.onenode.dropin

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.google.android.material.textfield.TextInputEditText
import com.google.gson.Gson
import com.onenode.dropin.service.ReceiverService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.InetSocketAddress
import java.net.Socket

class MainActivity : AppCompatActivity() {

    private lateinit var tvStatus: TextView
    private lateinit var etCode: TextInputEditText
    private lateinit var etIp: TextInputEditText
    private lateinit var btnPair: Button
    private lateinit var btnUnlink: Button
    private lateinit var btnSendFile: Button

    private lateinit var nsdManager: NsdManager
    private val serviceType = "_onenode._tcp"
    private var isDiscovering = false

    private val discoveryListener = object : NsdManager.DiscoveryListener {
        override fun onStartDiscoveryFailed(s: String, i: Int) {
            Log.e("OneNode", "Discovery start failed: $i")
            isDiscovering = false
        }
        override fun onStopDiscoveryFailed(s: String, i: Int) {
            Log.e("OneNode", "Discovery stop failed: $i")
            isDiscovering = false
        }
        override fun onDiscoveryStarted(s: String) {
            Log.d("OneNode", "Discovery started")
        }
        override fun onDiscoveryStopped(s: String) {
            Log.d("OneNode", "Discovery stopped")
        }
        override fun onServiceFound(info: NsdServiceInfo) {
            Log.d("OneNode", "Service found: ${info.serviceName}")
            // Match our service type (sometimes it has a trailing dot)
            if (info.serviceType.contains(serviceType)) {
                nsdManager.resolveService(info, resolveListener)
            }
        }
        override fun onServiceLost(info: NsdServiceInfo) {
            Log.d("OneNode", "Service lost: ${info.serviceName}")
        }
    }

    private val resolveListener = object : NsdManager.ResolveListener {
        override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) {
            Log.e("OneNode", "Resolve failed: $errorCode")
            // Retry after short delay
            android.os.Handler(mainLooper).postDelayed({
                try {
                    nsdManager.resolveService(info, this)
                } catch (e: Exception) {
                    Log.e("OneNode", "Retry resolve failed: ${e.message}")
                }
            }, 1000)
        }
        override fun onServiceResolved(resolved: NsdServiceInfo) {
            Log.d("OneNode", "Resolved! Host: ${resolved.host} Port: ${resolved.port}")
            val discoveredIp = resolved.host.hostAddress?.replace("/::ffff:", "")?.replace("/", "") ?: return
            runOnUiThread {
                if (etIp.text.isNullOrEmpty()) {
                    etIp.setText(discoveredIp)
                }
                if (prefs.getString("pairing_token", null) == null) {
                    tvStatus.text = "✅ Desktop found at $discoveredIp"
                }
            }
        }
    }

    private val prefs by lazy {
        getSharedPreferences("OneNodePrefs", Context.MODE_PRIVATE)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        tvStatus = findViewById(R.id.tvStatus)
        etCode = findViewById(R.id.etCode)
        etIp = findViewById(R.id.etIp)
        btnPair = findViewById(R.id.btnPair)
        btnUnlink = findViewById(R.id.btnUnlink)
        btnSendFile = findViewById(R.id.btnSendFile)

        btnPair.setOnClickListener { attemptPairing() }
        btnUnlink.setOnClickListener { unlink() }
        btnSendFile.setOnClickListener { pickFile() }

        checkPermissions()
        updateUiState()
        setupNsd()
    }

    private fun setupNsd() {
        nsdManager = getSystemService(Context.NSD_SERVICE) as NsdManager
        startDiscovery()
    }

    private fun startDiscovery() {
        Log.d("OneNode", "Starting NSD discovery...")
        tvStatus.text = "🔍 Looking for desktop..."
        try {
            nsdManager.discoverServices(serviceType, NsdManager.PROTOCOL_DNS_SD, discoveryListener)
            isDiscovering = true
        } catch (e: Exception) {
            Log.e("OneNode", "Error starting discovery: ${e.message}")
        }
    }

    private fun stopDiscovery() {
        if (isDiscovering) {
            try {
                nsdManager.stopServiceDiscovery(discoveryListener)
                isDiscovering = false
            } catch (e: Exception) {
                Log.e("OneNode", "Stop discovery error: ${e.message}")
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopDiscovery()
    }

    private fun checkPermissions() {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.P) {
            permissions.add(Manifest.permission.WRITE_EXTERNAL_STORAGE)
        }

        val toRequest = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (toRequest.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, toRequest.toTypedArray(), 100)
        }
    }

    private fun updateUiState() {
        val pairingToken = prefs.getString("pairing_token", null)
        val deviceName = prefs.getString("device_name", "Desktop")

        if (pairingToken != null) {
            tvStatus.text = "Linked to $deviceName"
            btnPair.visibility = View.GONE
            btnUnlink.visibility = View.VISIBLE
            btnSendFile.visibility = View.VISIBLE
            etCode.isEnabled = false
            etIp.isEnabled = false
            
            // Start the service if paired
            val intent = Intent(this, ReceiverService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(intent)
            } else {
                startService(intent)
            }
        } else {
            tvStatus.text = "Not linked"
            btnPair.visibility = View.VISIBLE
            btnUnlink.visibility = View.GONE
            btnSendFile.visibility = View.GONE
            etCode.isEnabled = true
            etIp.isEnabled = true
            btnPair.isEnabled = true
        }
    }

    private fun attemptPairing() {
        val code = etCode.text?.toString()?.trim() ?: ""
        val ip = etIp.text?.toString()?.trim() ?: ""

        if (code.length != 6) {
            Toast.makeText(this, "Enter a 6-digit code", Toast.LENGTH_SHORT).show()
            return
        }
        if (ip.isEmpty()) {
            Toast.makeText(this, "Enter the desktop IP address", Toast.LENGTH_SHORT).show()
            return
        }

        tvStatus.text = "Connecting to One Node..."
        btnPair.isEnabled = false

        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    val socket = Socket()
                    socket.connect(InetSocketAddress(ip, 45678), 10000)
                    
                    val writer = socket.getOutputStream().bufferedWriter()
                    val reader = socket.getInputStream().bufferedReader()

                    val request = mapOf(
                        "code" to code,
                        "device" to Build.MODEL
                    )
                    writer.write(Gson().toJson(request))
                    writer.newLine()
                    writer.flush()

                    val response = reader.readLine()
                    socket.close()

                    if (response == null) throw Exception("No response from desktop")
                    Gson().fromJson(response, Map::class.java)
                }
            }

            result.onSuccess { response ->
                if (response["status"] == "ok") {
                    val token = response["token"] as String
                    prefs.edit()
                        .putString("pairing_token", token)
                        .putString("device_ip", ip)
                        .putString("device_name", "Desktop")
                        .apply()
                    
                    updateUiState()
                    Toast.makeText(this@MainActivity, "Linked successfully", Toast.LENGTH_SHORT).show()
                } else {
                    val message = response["message"] as? String ?: "Pairing failed"
                    tvStatus.text = "Error: $message"
                    btnPair.isEnabled = true
                }
            }.onFailure { e ->
                tvStatus.text = "Connection failed: ${e.message}"
                btnPair.isEnabled = true
            }
        }
    }

    private fun unlink() {
        prefs.edit().clear().apply()
        stopService(Intent(this, ReceiverService::class.java))
        updateUiState()
        btnSendFile.visibility = View.GONE
        etCode.text?.clear()
        etIp.text?.clear()
        Toast.makeText(this, "Unlinked from One Node", Toast.LENGTH_SHORT).show()
    }

    private val filePickerLauncher = registerForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri ->
        uri?.let { sendFileToDeskop(it) }
    }

    private fun pickFile() {
        filePickerLauncher.launch("*/*")
    }

    private fun sendFileToDeskop(uri: Uri) {
        val desktopIp = prefs.getString("device_ip", null) ?: run {
            tvStatus.text = "Not linked"
            return
        }

        tvStatus.text = "Sending..."

        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    // Get filename
                    val fileName = getFileName(uri) ?: "shared_file"

                    // Get file bytes
                    val inputStream = contentResolver.openInputStream(uri)
                        ?: throw Exception("Cannot open file")
                    val fileBytes = inputStream.readBytes()
                    inputStream.close()

                    val socket = java.net.Socket(desktopIp, 45680)
                    val output = socket.getOutputStream()

                    val nameBytes = fileName.toByteArray(Charsets.UTF_8)
                    val nameLen = nameBytes.size
                    val fileSize = fileBytes.size.toLong()

                    // Header: 4 bytes name length
                    output.write(byteArrayOf(
                        (nameLen shr 24).toByte(),
                        (nameLen shr 16).toByte(),
                        (nameLen shr 8).toByte(),
                        nameLen.toByte()
                    ))
                    // Name bytes
                    output.write(nameBytes)
                    // 8 bytes file size
                    output.write(byteArrayOf(
                        (fileSize shr 56).toByte(),
                        (fileSize shr 48).toByte(),
                        (fileSize shr 40).toByte(),
                        (fileSize shr 32).toByte(),
                        (fileSize shr 24).toByte(),
                        (fileSize shr 16).toByte(),
                        (fileSize shr 8).toByte(),
                        fileSize.toByte()
                    ))
                    // File data
                    output.write(fileBytes)
                    output.flush()
                    socket.close()

                    fileName
                }
            }

            result.onSuccess { fileName ->
                tvStatus.text = "✅ Sent: $fileName"
            }
            result.onFailure {
                tvStatus.text = "❌ Failed: ${it.message}"
            }
        }
    }

    private fun getFileName(uri: Uri): String? {
        var name: String? = null
        contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (cursor.moveToFirst() && nameIndex >= 0) {
                name = cursor.getString(nameIndex)
            }
        }
        return name ?: uri.lastPathSegment
    }
}
