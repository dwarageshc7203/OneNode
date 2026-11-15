package app.onenode

import android.Manifest
import android.app.DownloadManager
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import app.onenode.ui.HomeScreen
import app.onenode.ui.OneNodeTheme

class MainActivity : ComponentActivity() {
    private val engine get() = (application as OneNodeApp).engine

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        handleShare(intent)
        if (savedInstanceState == null && engine.wantedReceiving()) engine.startReceiving() // as the user left it

        setContent {
            OneNodeTheme {
                var target by remember { mutableStateOf<Target?>(null) }
                val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
                    val t = target
                    target = null
                    if (t != null && uris.isNotEmpty()) engine.send(uris, t)
                }
                // Android 13+ asks before showing a notification. Receiving works either way; this only shows the notification.
                val notifications = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
                    engine.startReceiving()
                }
                HomeScreen(
                    engine = engine,
                    onPickFiles = { t -> target = t; picker.launch(arrayOf("*/*")) },
                    onTurnOnReceiving = {
                        if (Build.VERSION.SDK_INT >= 33 &&
                            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
                        ) {
                            notifications.launch(Manifest.permission.POST_NOTIFICATIONS)
                        } else {
                            engine.startReceiving()
                        }
                    },
                    onOpenDownloads = { startActivity(Intent(DownloadManager.ACTION_VIEW_DOWNLOADS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) },
                )
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleShare(intent)
    }

    // The searching for nearby devices only runs while the screen is visible.
    override fun onStart() {
        super.onStart()
        engine.screenShown()
    }

    override fun onStop() {
        engine.screenHidden()
        super.onStop()
    }

    // "Share > OneNode" from another app: remember the files, the screen then asks where to send them.
    private fun handleShare(intent: Intent?) {
        intent ?: return
        val uris: List<Uri> = when (intent.action) {
            Intent.ACTION_SEND -> listOfNotNull(IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri::class.java))
            Intent.ACTION_SEND_MULTIPLE -> IntentCompat.getParcelableArrayListExtra(intent, Intent.EXTRA_STREAM, Uri::class.java).orEmpty()
            else -> return
        }
        if (uris.isNotEmpty()) engine.setShared(uris)
    }
}
