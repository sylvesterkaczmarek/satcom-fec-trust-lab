package com.sylvesterkaczmarek.satcomfec.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun MetricsScreen() {
    Column(
        modifier = Modifier.padding(16.dp)
    ) {
        Text(text = "Metrics")
        Text(
            text = "This will display decoder performance metrics, trust scores, and signal quality indicators."
        )
    }
}

