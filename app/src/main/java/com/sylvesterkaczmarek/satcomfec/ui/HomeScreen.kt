package com.sylvesterkaczmarek.satcomfec.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun HomeScreen() {
    Column(
        modifier = Modifier.padding(16.dp)
    ) {
        Text(text = "Home")
        Text(
            text = "This will show basic status: demo IQ file loaded, decoder mode (SME2 vs NEON), and a simple trust score."
        )
    }
}
