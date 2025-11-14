package com.sylvesterkaczmarek.satcomfec

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import com.sylvesterkaczmarek.satcomfec.ui.HomeScreen
import com.sylvesterkaczmarek.satcomfec.ui.MetricsScreen
import com.sylvesterkaczmarek.satcomfec.ui.SettingsScreen

class MainActivity : ComponentActivity() {

    companion object {
        init {
            System.loadLibrary("satcomfec")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            SatcomFecApp()
        }
    }
}

private enum class Screen {
    HOME,
    METRICS,
    SETTINGS
}

@Composable
private fun SatcomFecApp() {
    val currentScreen = remember { mutableStateOf(Screen.HOME) }

    MaterialTheme {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = {
                        Text(text = "Satcom FEC Trust Lab")
                    }
                )
            },
            content = { paddingValues ->
                Column(
                    modifier = Modifier
                        .fillMaxSize(),
                    verticalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(
                        modifier = Modifier
                            .weight(1f),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Center
                    ) {
                        when (currentScreen.value) {
                            Screen.HOME -> HomeScreen()
                            Screen.METRICS -> MetricsScreen()
                            Screen.SETTINGS -> SettingsScreen()
                        }
                    }

                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Bottom
                    ) {
                        NavButtons(
                            onHome = { currentScreen.value = Screen.HOME },
                            onMetrics = { currentScreen.value = Screen.METRICS },
                            onSettings = { currentScreen.value = Screen.SETTINGS }
                        )
                    }
                }
            }
        )
    }
}

@Composable
private fun NavButtons(
    onHome: () -> Unit,
    onMetrics: () -> Unit,
    onSettings: () -> Unit
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.SpaceAround
    ) {
        Button(onClick = onHome) {
            Text(text = "Home")
        }
        Button(onClick = onMetrics) {
            Text(text = "Metrics")
        }
        Button(onClick = onSettings) {
            Text(text = "Settings")
        }
    }
}
