package com.sylvesterkaczmarek.satcomfec.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun SettingsScreen() {
    Column(
        modifier = Modifier.padding(16.dp)
    ) {
        Text(text = "Settings")
        Text(
            text = "This will hold decoder options (LDPC / Viterbi), SME2 vs NEON selection, and demo data presets."
        )
    }
}
