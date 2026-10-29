package au.obdojo.app

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.material.icons.filled.VisibilityOff
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.foundation.background
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.border
import androidx.compose.ui.draw.clip

object C {
    val Accent = Color(0xFF6CC24A)
    val Accent2 = Color(0xFF8FD66E)
    val Bg = Color(0xFF0A0A0A)
    val Card = Color(0xFF151515)
    val Card2 = Color(0xFF1D1D1D)
    val Line = Color(0xFF2B2B2B)
    val Text = Color(0xFFF2F2F2)
    val Muted = Color(0xFF9A9A9A)
    val Bad = Color(0xFFFF3B30)
    val Ok = Color(0xFF30D158)
    val Warn = Color(0xFFFFCC00)
    val Track = Color(0xFF262626)

    /** Lime to deep green, like the throwing star on the app icon. */
    val Glow = androidx.compose.ui.graphics.Brush.horizontalGradient(listOf(Color(0xFF9BE07A), Accent, Color(0xFF3E9A2A)))
}

private val scheme = darkColorScheme(
    primary = C.Accent, onPrimary = Color.Black,
    secondary = C.Accent2, onSecondary = Color.Black,
    background = C.Bg, onBackground = C.Text,
    surface = C.Card, onSurface = C.Text,
    surfaceVariant = C.Card2, onSurfaceVariant = C.Muted,
    outline = C.Line, error = C.Bad
)

@Composable
fun ObdojoTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = scheme, content = content)
}

/** Bold italic upper-case heading */
@Composable
fun Heading(text: String, modifier: Modifier = Modifier) {
    Text(
        text.uppercase(), modifier = modifier.padding(top = 18.dp, bottom = 10.dp),
        style = TextStyle(
            color = C.Accent, fontSize = 14.sp, fontWeight = FontWeight.ExtraBold,
            fontStyle = FontStyle.Italic, letterSpacing = 1.sp
        )
    )
}

@Composable
fun CardBox(modifier: Modifier = Modifier, borderColor: Color = C.Line, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .background(C.Card)
            .border(1.dp, borderColor, RoundedCornerShape(12.dp))
            .padding(14.dp),
        content = content
    )
}

@Composable
fun Logo() {
    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        androidx.compose.foundation.Image(
            androidx.compose.ui.res.painterResource(R.drawable.obdojo_icon), contentDescription = null,
            modifier = Modifier.size(34.dp).clip(RoundedCornerShape(8.dp))
        )
        Spacer(Modifier.width(10.dp))
        Wordmark(26)
    }
}

/** "OBDojo" in heavy italic: OBD white, ojo green. */
@Composable
fun Wordmark(size: Int) {
    Row {
        Text(
            "OBD", color = C.Text, fontSize = size.sp,
            fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, letterSpacing = 0.5.sp
        )
        Text(
            "ojo", color = C.Accent, fontSize = size.sp,
            fontWeight = FontWeight.Black, fontStyle = FontStyle.Italic, letterSpacing = 0.5.sp
        )
    }
}

@Composable
fun KButton(
    text: String, onClick: () -> Unit, modifier: Modifier = Modifier,
    primary: Boolean = false, danger: Boolean = false, enabled: Boolean = true
) {
    val bg = when { danger -> C.Bad; primary -> C.Accent; else -> C.Card2 }
    val fg = when { danger -> Color.White; primary -> Color.Black; else -> C.Text }
    Button(
        onClick = onClick, enabled = enabled, modifier = modifier,
        shape = RoundedCornerShape(10.dp),
        colors = ButtonDefaults.buttonColors(containerColor = bg, contentColor = fg),
        border = if (!primary && !danger) androidx.compose.foundation.BorderStroke(1.dp, C.Line) else null,
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 12.dp)
    ) {
        Text(text.uppercase(), fontWeight = FontWeight.ExtraBold, fontStyle = FontStyle.Italic, letterSpacing = 0.5.sp)
    }
}

/** Text field for passwords, with an eye button to show / hide what's typed */
@Composable
fun PasswordField(
    value: String, onValueChange: (String) -> Unit, label: String,
    modifier: Modifier = Modifier, placeholder: String? = null
) {
    var shown by androidx.compose.runtime.saveable.rememberSaveable { androidx.compose.runtime.mutableStateOf(false) }
    OutlinedTextField(
        value, onValueChange, label = { Text(label) }, singleLine = true,
        placeholder = if (placeholder != null) { { Text(placeholder) } } else null,
        visualTransformation = if (shown) androidx.compose.ui.text.input.VisualTransformation.None
            else androidx.compose.ui.text.input.PasswordVisualTransformation(),
        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(
            keyboardType = if (shown) androidx.compose.ui.text.input.KeyboardType.Text
                else androidx.compose.ui.text.input.KeyboardType.Password
        ),
        trailingIcon = {
            IconButton({ shown = !shown }) {
                Icon(
                    if (shown) Icons.Filled.VisibilityOff else Icons.Filled.Visibility,
                    contentDescription = if (shown) "Hide password" else "Show password",
                    tint = if (shown) C.Accent else C.Muted
                )
            }
        },
        modifier = modifier
    )
}
