package app.onenode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Snackbar
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.onenode.Discovery
import app.onenode.Engine
import app.onenode.Target
import app.onenode.TransferItem
import app.onenode.TransferState
import app.onenode.core.TrustStore

// One screen: receive switch, nearby devices, paired devices, activity. The same four things as the desktop home page.
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    engine: Engine,
    onPickFiles: (Target) -> Unit,       // the user chose a target: open the file picker
    onTurnOnReceiving: () -> Unit,        // asks for the notification permission first when Android needs it
    onOpenDownloads: () -> Unit,
) {
    val name by engine.name.collectAsState()
    val receiving by engine.receiving.collectAsState()
    val problem by engine.problem.collectAsState()
    val pairing by engine.pairing.collectAsState()
    val nearby by engine.nearby.collectAsState()
    val paired by engine.paired.collectAsState()
    val transfers by engine.transfers.collectAsState()
    val pinRequest by engine.pinRequest.collectAsState()
    val shared by engine.shared.collectAsState()

    var editName by remember { mutableStateOf(false) }
    var addressDialog by remember { mutableStateOf(false) }
    var forgetting by remember { mutableStateOf<TrustStore.Device?>(null) }
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(Unit) { engine.toasts.collect { snackbar.showSnackbar(it) } }

    fun choose(target: Target) {
        if (shared.isNotEmpty()) engine.sendShared(target) else onPickFiles(target)
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        snackbarHost = { SnackbarHost(snackbar) { Snackbar(it) } },
    ) { inner ->
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = inner.calculateTopPadding() + 12.dp, bottom = inner.calculateBottomPadding() + 24.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            // ---- title and this device ----
            item {
                Column(Modifier.widthIn(max = 760.dp)) {
                    Text("OneNode", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold, color = MaterialTheme.colorScheme.primary)
                    Text("Send files to devices on the same Wi-Fi. No cloud, no account.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }

            problem?.let { text ->
                item {
                    OneCard {
                        RowSpaced {
                            Text(text, Modifier.weight(1f), color = MaterialTheme.colorScheme.error)
                            TextButton(onClick = engine::dismissProblem) { Text("OK") }
                        }
                    }
                }
            }

            if (shared.isNotEmpty()) {
                item {
                    OneCard {
                        RowSpaced {
                            Text(
                                "Choose where to send ${shared.size} file${if (shared.size == 1) "" else "s"}: tap a device below.",
                                Modifier.weight(1f), fontWeight = FontWeight.SemiBold,
                            )
                            TextButton(onClick = { engine.setShared(emptyList()) }) { Text("Cancel") }
                        }
                    }
                }
            }

            item {
                OneCard {
                    RowSpaced {
                        Stack(Modifier.weight(1f)) {
                            Text("This device", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Text(name, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        }
                        TextButton(onClick = { editName = true }) { Text("Rename") }
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline)
                    RowSpaced {
                        Stack(Modifier.weight(1f)) {
                            Text("Receive files", fontWeight = FontWeight.SemiBold)
                            Text(
                                if (receiving) "On: visible to nearby devices. Saving to Downloads/OneNode." else "Off: other devices cannot reach this phone.",
                                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Switch(checked = receiving, onCheckedChange = { on -> if (on) onTurnOnReceiving() else engine.stopReceiving() })
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outline)
                    RowSpaced {
                        Stack(Modifier.weight(1f)) {
                            Text("Pair a new device", fontWeight = FontWeight.SemiBold)
                            Text(
                                "Shows a 6-digit PIN for the other device to type. Needed once per device.",
                                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Button(onClick = engine::startPairing) { Text("Pair") }
                    }
                }
            }

            // ---- nearby ----
            item { SectionTitle("Nearby devices") }
            if (nearby.isEmpty()) {
                item {
                    OneCard {
                        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            Text("Looking for devices on this Wi-Fi…", fontWeight = FontWeight.SemiBold)
                            Text(
                                "Open OneNode on the other device and turn on Receive. If nothing shows up, some networks block device discovery: send to an address instead.",
                                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            } else {
                items(nearby, key = { it.serviceName }) { peer ->
                    val isPaired = paired.any { it.fingerprint == peer.fingerprint }
                    NearbyRow(peer, isPaired) { choose(Target(peer.host, peer.port, peer.name, peer.fingerprint)) }
                }
            }
            item {
                OutlinedButton(onClick = { addressDialog = true }, modifier = Modifier.fillMaxWidth()) { Text("Send to an address…") }
            }

            // ---- paired ----
            item { SectionTitle("Paired devices") }
            if (paired.isEmpty()) {
                item {
                    OneCard {
                        Text(
                            "None yet. Pair a device once and it is remembered; after that no PIN is needed.",
                            Modifier.padding(16.dp), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            } else {
                items(paired, key = { it.fingerprint }) { device ->
                    OneCard {
                        RowSpaced {
                            Stack(Modifier.weight(1f)) {
                                Text(device.name, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(
                                    "ID " + device.fingerprint.take(8) + "…",
                                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                            TextButton(onClick = { forgetting = device }) { Text("Remove", color = MaterialTheme.colorScheme.error) }
                        }
                    }
                }
            }

            // ---- activity ----
            if (transfers.isNotEmpty()) {
                item {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        SectionTitle("Activity", Modifier.weight(1f))
                        TextButton(onClick = onOpenDownloads) { Text("Open Downloads") }
                        TextButton(onClick = engine::clearFinished) { Text("Clear") }
                    }
                }
                items(transfers, key = { it.id }) { t -> TransferRow(t) { engine.cancelTransfer(t.id) } }
            }
        }
    }

    pairing?.let { PairDialog(it, onCancel = engine::cancelPairing) }
    pinRequest?.let { PinDialog(it, onSubmit = { text -> engine.submitPin(it, text) }, onCancel = { engine.cancelPin(it) }) }
    if (editName) NameDialog(name, onSave = { engine.rename(it); editName = false }, onCancel = { editName = false })
    if (addressDialog) {
        AddressDialog(
            onSend = { host, port -> addressDialog = false; choose(Target(host, port, host, null)) },
            onCancel = { addressDialog = false },
        )
    }
    forgetting?.let { d ->
        ConfirmDialog(
            "Remove ${d.name}?", "It will need a new PIN the next time you send to it or receive from it.", "Remove",
            onConfirm = { engine.forget(d.fingerprint); forgetting = null }, onCancel = { forgetting = null },
        )
    }
}

@Composable
private fun NearbyRow(peer: Discovery.Peer, paired: Boolean, onClick: () -> Unit) {
    OneCard(Modifier.clickable(onClick = onClick)) {
        RowSpaced {
            Stack(Modifier.weight(1f)) {
                Text(peer.name, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(peer.host, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            when {
                paired -> Chip("Paired")
                peer.pairing -> Chip("Waiting to pair")
            }
        }
    }
}

@Composable
private fun TransferRow(t: TransferItem, onCancel: () -> Unit) {
    OneCard {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Stack(Modifier.weight(1f)) {
                    Text(t.fileName, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(
                        (if (t.sending) "To " else "From ") + t.peer,
                        style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    )
                }
                if (t.sending && (t.state == TransferState.RUNNING || t.state == TransferState.PAIRING || t.state == TransferState.WAITING)) {
                    TextButton(onClick = onCancel) { Text("Cancel") }
                }
            }
            when (t.state) {
                TransferState.WAITING, TransferState.PAIRING -> {
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                    Text(if (t.state == TransferState.PAIRING) "Waiting for the PIN…" else "Waiting…", style = MaterialTheme.typography.bodySmall)
                }
                TransferState.RUNNING -> {
                    if (t.total > 0) LinearProgressIndicator(progress = { (t.done.toFloat() / t.total).coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth())
                    else LinearProgressIndicator(Modifier.fillMaxWidth())
                    Text("${size(t.done)} of ${size(t.total)}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                TransferState.DONE -> Text(t.message, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
                TransferState.FAILED -> Text(t.message, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
                TransferState.CANCELLED -> Text(t.message, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

internal fun size(bytes: Long): String = when {
    bytes < 1024 -> "$bytes B"
    bytes < 1024 * 1024 -> String.format(java.util.Locale.US, "%.1f KB", bytes / 1024.0)
    bytes < 1024L * 1024 * 1024 -> String.format(java.util.Locale.US, "%.1f MB", bytes / 1024.0 / 1024.0)
    else -> String.format(java.util.Locale.US, "%.2f GB", bytes / 1024.0 / 1024.0 / 1024.0)
}
