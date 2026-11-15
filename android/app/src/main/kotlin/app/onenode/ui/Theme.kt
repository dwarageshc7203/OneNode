package app.onenode.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// The same palette as the desktop app (qml/Theme.qml): one green on a very light green-grey page.
private val Green = Color(0xFF15803D)
private val Light = lightColorScheme(
    primary = Green,
    onPrimary = Color.White,
    primaryContainer = Color(0xFFE6F4EA),
    onPrimaryContainer = Color(0xFF166534),
    background = Color(0xFFF6F9F7),
    onBackground = Color(0xFF14201A),
    surface = Color.White,
    onSurface = Color(0xFF14201A),
    surfaceVariant = Color(0xFFE6F4EA),
    onSurfaceVariant = Color(0xFF55655C),
    outline = Color(0xFFE1E8E3),
    outlineVariant = Color(0xFFD5E0D8),
    error = Color(0xFFB91C1C),
    errorContainer = Color(0xFFFDECEC),
    onErrorContainer = Color(0xFF7F1D1D),
)
private val Dark = darkColorScheme(
    primary = Color(0xFF4ADE80),
    onPrimary = Color(0xFF052E16),
    primaryContainer = Color(0xFF14532D),
    onPrimaryContainer = Color(0xFFBBF7D0),
    background = Color(0xFF0F1612),
    onBackground = Color(0xFFE6EEE9),
    surface = Color(0xFF17201B),
    onSurface = Color(0xFFE6EEE9),
    surfaceVariant = Color(0xFF1E2B24),
    onSurfaceVariant = Color(0xFFA7B8AE),
    outline = Color(0xFF2B3A32),
    outlineVariant = Color(0xFF34453B),
    error = Color(0xFFF87171),
    errorContainer = Color(0xFF450A0A),
    onErrorContainer = Color(0xFFFECACA),
)

@Composable
fun OneNodeTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Dark else Light, content = content)
}
