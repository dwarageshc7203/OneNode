package app.onenode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.onenode.Address
import app.onenode.PairingUi
import app.onenode.PinRequest
import kotlinx.coroutines.delay

// The receiving side: shows the PIN and how long it lives. Nothing here can be started from the network.
@Composable
fun PairDialog(pairing: PairingUi, onCancel: () -> Unit) {
    var left by remember(pairing) { mutableLongStateOf(secondsLeft(pairing)) }
    LaunchedEffect(pairing) {
        while (left > 0) {
            delay(500)
            left = secondsLeft(pairing)
        }
    }
    AlertDialog(
        onDismissRequest = {},
        title = { Text("Pair a new device") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("On the other device, choose this phone and type this PIN:")
                Text(
                    pairing.pinText, fontSize = 40.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.primary, modifier = Modifier.fillMaxWidth(),
                )
                Text("Valid for ${left}s, for one device only.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        },
        confirmButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

private fun secondsLeft(p: PairingUi) = ((p.endsAtMs - System.currentTimeMillis()) / 1000).coerceAtLeast(0)

// The sending side: the receiver asked for a PIN.
@Composable
fun PinDialog(request: PinRequest, onSubmit: (String) -> Unit, onCancel: () -> Unit) {
    var text by remember(request) { mutableStateOf("") }
    val focus = remember { FocusRequester() }
    LaunchedEffect(request) { focus.requestFocus() }
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Enter the PIN") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("This is the first time with ${request.peerName}. On ${request.peerName}, choose \"Pair a new device\" and type the 6 digits it shows.")
                OutlinedTextField(
                    value = text,
                    onValueChange = { v -> text = v.filter { it.isDigit() }.take(6) },
                    singleLine = true,
                    label = { Text("6-digit PIN") },
                    isError = request.problem != null,
                    supportingText = request.problem?.let { { Text(it) } },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth().focusRequester(focus),
                )
            }
        },
        confirmButton = { TextButton(onClick = { onSubmit(text) }, enabled = text.length == 6) { Text("Pair and send") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun NameDialog(current: String, onSave: (String) -> Unit, onCancel: () -> Unit) {
    var text by remember { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("This device's name") },
        text = {
            OutlinedTextField(value = text, onValueChange = { text = it.take(64) }, singleLine = true, modifier = Modifier.fillMaxWidth())
        },
        confirmButton = { TextButton(onClick = { onSave(text) }, enabled = text.isNotBlank()) { Text("Save") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun AddressDialog(onSend: (host: String, port: Int) -> Unit, onCancel: () -> Unit) {
    var text by remember { mutableStateOf("") }
    val parsed = Address.parse(text)
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text("Send to an address") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Type the other device's address, for example 192.168.1.20 (or 192.168.1.20:45454).")
                OutlinedTextField(
                    value = text, onValueChange = { text = it }, singleLine = true, label = { Text("Address") },
                    isError = text.isNotBlank() && parsed == null,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = { TextButton(onClick = { parsed?.let { onSend(it.host, it.port) } }, enabled = parsed != null) { Text("Choose files") } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}

@Composable
fun ConfirmDialog(title: String, message: String, confirm: String, onConfirm: () -> Unit, onCancel: () -> Unit) {
    AlertDialog(
        onDismissRequest = onCancel,
        title = { Text(title) },
        text = { Text(message) },
        confirmButton = { TextButton(onClick = onConfirm) { Text(confirm) } },
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
}
