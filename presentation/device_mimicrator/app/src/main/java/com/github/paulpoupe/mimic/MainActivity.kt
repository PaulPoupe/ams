package com.github.paulpoupe.mimic

import android.content.Context
import android.content.SharedPreferences
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.github.paulpoupe.mimic.ui.theme.MimicTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import kotlin.math.roundToInt
import kotlin.math.roundToLong
import kotlin.random.Random

private const val PREFS_NAME = "device_mimic_prefs"
private const val BASE_URL_KEY = "base_url"
private const val DEFAULT_BASE_URL = "http://10.0.2.2"
private const val REQUIRED_DEMO_DEVICE_COUNT = 3
private const val TDOA_LIMIT_US = 120_000f
private const val TDOA_STEP_US = 5_000f
private const val TDOA_SLIDER_STEPS = 47

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val apiClient = BackendApiClient()
        val audioProvider = RawAudioProvider(this)
        val healthPayloads = HealthPayloadFactory()

        setContent {
            MimicTheme {
                MimicApp(
                    prefs = prefs,
                    apiClient = apiClient,
                    audioProvider = audioProvider,
                    healthPayloads = healthPayloads,
                )
            }
        }
    }
}

@Composable
private fun MimicApp(
    prefs: SharedPreferences,
    apiClient: BackendApiClient,
    audioProvider: RawAudioProvider,
    healthPayloads: HealthPayloadFactory,
) {
    val scope = rememberCoroutineScope()
    val selectedDemoIds = remember { mutableStateListOf<String>() }
    val sendingIds = remember { mutableStateListOf<String>() }
    val healthEnabledIds = remember { mutableStateListOf<String>() }
    val clapMessages = remember { mutableStateMapOf<String, String>() }
    val healthMessages = remember { mutableStateMapOf<String, String>() }
    val healthJobs = remember { mutableMapOf<String, Job>() }

    var baseUrl by rememberSaveable {
        mutableStateOf(prefs.getString(BASE_URL_KEY, DEFAULT_BASE_URL) ?: DEFAULT_BASE_URL)
    }
    var devices by remember { mutableStateOf<List<DeviceDto>>(emptyList()) }
    var status by remember { mutableStateOf("Set backend URL and refresh devices.") }
    var isLoading by remember { mutableStateOf(false) }
    var isDemoRunning by remember { mutableStateOf(false) }
    var eastWestUs by rememberSaveable { mutableStateOf(0f) }
    var northSouthUs by rememberSaveable { mutableStateOf(0f) }

    DisposableEffect(Unit) {
        onDispose {
            healthJobs.values.forEach { it.cancel() }
            healthJobs.clear()
        }
    }

    fun saveBaseUrl() {
        prefs.edit().putString(BASE_URL_KEY, baseUrl.trim()).apply()
    }

    fun refreshDevices() {
        saveBaseUrl()
        isLoading = true
        status = "Loading devices..."
        scope.launch {
            runCatching {
                withContext(Dispatchers.IO) {
                    apiClient.listDevices(baseUrl)
                }
            }.onSuccess { loadedDevices ->
                devices = loadedDevices
                val loadedIds = loadedDevices.map { it.id }.toSet()
                selectedDemoIds.removeAll { it !in loadedIds }
                status = if (loadedDevices.isEmpty()) {
                    "No registered devices."
                } else {
                    "Loaded ${loadedDevices.size} device(s)."
                }
            }.onFailure { error ->
                devices = emptyList()
                selectedDemoIds.clear()
                status = "Load failed: ${error.message ?: "unknown error"}"
            }
            isLoading = false
        }
    }

    fun sendDeviceEvent(device: DeviceDto, eventTimeNs: Long = currentEpochNs()) {
        saveBaseUrl()
        if (!sendingIds.contains(device.id)) {
            sendingIds.add(device.id)
        }
        clapMessages[device.id] = "Sending..."
        status = "Sending event from ${device.id}..."

        scope.launch {
            runCatching {
                withContext(Dispatchers.IO) {
                    sendEventWithAudio(apiClient, audioProvider, baseUrl, device, eventTimeNs)
                }
            }.onSuccess { result ->
                clapMessages[device.id] = if (result.audioName == null) {
                    "Event #${result.eventId}; no audio resource."
                } else {
                    "Event #${result.eventId}; audio ${result.audioName}."
                }
                status = "Sent event #${result.eventId} from ${device.id}."
            }.onFailure { error ->
                clapMessages[device.id] = "Failed: ${error.message ?: "unknown error"}"
                status = "Send failed: ${error.message ?: "unknown error"}"
            }
            sendingIds.remove(device.id)
        }
    }

    fun demonstrateSelectedDevices() {
        saveBaseUrl()
        val selectedDevices = devices.filter { it.id in selectedDemoIds }
        if (selectedDevices.size != REQUIRED_DEMO_DEVICE_COUNT) {
            status = "Select exactly $REQUIRED_DEMO_DEVICE_COUNT devices."
            return
        }

        isDemoRunning = true
        selectedDevices.forEach { device ->
            if (!sendingIds.contains(device.id)) {
                sendingIds.add(device.id)
            }
            clapMessages[device.id] = "Demo queued..."
        }
        status = "Complex demo started for ${selectedDevices.size} devices."

        scope.launch {
            val baseEventTimeNs = currentEpochNs() + Random.nextLong(6_000_000L, 20_000_000L)
            val arrivalOffsetsNs = calculateArrivalOffsetsNs(
                devices = selectedDevices,
                eastWestUs = eastWestUs,
                northSouthUs = northSouthUs,
            )
            val results = mutableListOf<String>()

            for (device in selectedDevices.shuffled()) {
                val networkDelayMs = Random.nextLong(80L, 701L)
                val deviceOffsetNs = arrivalOffsetsNs[device.id] ?: 0L
                val jitterNs = Random.nextLong(-120_000L, 120_001L)
                val eventTimeNs = baseEventTimeNs + deviceOffsetNs + jitterNs
                delay(networkDelayMs)
                clapMessages[device.id] =
                    "Net ${networkDelayMs}ms, TDOA ${deviceOffsetNs / 1_000L}us..."

                runCatching {
                    withContext(Dispatchers.IO) {
                        sendEventWithAudio(apiClient, audioProvider, baseUrl, device, eventTimeNs)
                    }
                }.onSuccess { result ->
                    val audioText = result.audioName?.let { ", audio $it" } ?: ", metadata only"
                    clapMessages[device.id] = "Demo event #${result.eventId}$audioText."
                    results.add("${device.id}: #${result.eventId}")
                }.onFailure { error ->
                    clapMessages[device.id] = "Demo failed: ${error.message ?: "unknown error"}"
                    results.add("${device.id}: failed")
                }

                sendingIds.remove(device.id)
            }

            status = "Complex demo finished: ${results.joinToString("; ")}"
            isDemoRunning = false
        }
    }

    fun setDemoSelected(device: DeviceDto, selected: Boolean) {
        if (selected) {
            if (selectedDemoIds.contains(device.id)) {
                return
            }
            if (selectedDemoIds.size >= REQUIRED_DEMO_DEVICE_COUNT) {
                status = "Only $REQUIRED_DEMO_DEVICE_COUNT devices can be selected for demo."
                return
            }
            selectedDemoIds.add(device.id)
        } else {
            selectedDemoIds.remove(device.id)
        }
    }

    fun setHealthLoop(device: DeviceDto, enabled: Boolean) {
        saveBaseUrl()

        if (enabled) {
            if (healthEnabledIds.contains(device.id)) {
                return
            }

            healthEnabledIds.add(device.id)
            healthMessages[device.id] = "Syncing time..."
            healthJobs[device.id]?.cancel()
            healthJobs[device.id] = scope.launch {
                var syncSummary = "Time sync pending"
                runCatching {
                    withContext(Dispatchers.IO) {
                        apiClient.syncTime(
                            baseUrl = baseUrl,
                            deviceId = device.id,
                            clientSentMonotonicNs = System.nanoTime(),
                        )
                    }
                }.onSuccess {
                    syncSummary = "Time synced"
                    healthMessages[device.id] = "$syncSummary; health loop running."
                }.onFailure { error ->
                    syncSummary = "Time sync failed: ${error.message ?: "unknown error"}"
                    healthMessages[device.id] = syncSummary
                }

                while (isActive) {
                    runCatching {
                        withContext(Dispatchers.IO) {
                            apiClient.sendHealth(
                                baseUrl = baseUrl,
                                deviceId = device.id,
                                payload = healthPayloads.next(device.id),
                            )
                        }
                    }.onSuccess {
                        healthMessages[device.id] = "$syncSummary; health and battery sent; next in about 2 min."
                    }.onFailure { error ->
                        healthMessages[device.id] = "$syncSummary; health failed: ${error.message ?: "unknown error"}"
                    }

                    delay(Random.nextLong(110_000L, 145_001L))
                }
            }
        } else {
            healthEnabledIds.remove(device.id)
            healthJobs.remove(device.id)?.cancel()
            healthMessages[device.id] = "Health loop off."
        }
    }

    val isNetworkBusy = isLoading || sendingIds.isNotEmpty() || isDemoRunning

    Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text("Device Sim", style = MaterialTheme.typography.titleLarge)

            OutlinedTextField(
                value = baseUrl,
                onValueChange = { baseUrl = it },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                label = { Text("Backend URL") },
                supportingText = { Text("Emulator: http://10.0.2.2, phone: http://LAN-IP") },
            )

            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Button(
                    onClick = ::refreshDevices,
                    enabled = !isNetworkBusy,
                ) {
                    Text("Refresh")
                }

                Button(
                    onClick = ::demonstrateSelectedDevices,
                    enabled = !isNetworkBusy && selectedDemoIds.size == REQUIRED_DEMO_DEVICE_COUNT,
                ) {
                    Text("Demo 3")
                }

                if (isLoading || isDemoRunning) {
                    CircularProgressIndicator()
                }
            }

            Text("Selected: ${selectedDemoIds.size}/$REQUIRED_DEMO_DEVICE_COUNT")
            Text(status)

            TdoaControls(
                eastWestUs = eastWestUs,
                northSouthUs = northSouthUs,
                onEastWestChange = { eastWestUs = it },
                onNorthSouthChange = { northSouthUs = it },
            )

            Spacer(modifier = Modifier.height(4.dp))

            LazyColumn(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(devices, key = { it.id }) { device ->
                    val selected = selectedDemoIds.contains(device.id)
                    DeviceRow(
                        device = device,
                        isDemoSelected = selected,
                        canSelectForDemo = selected || selectedDemoIds.size < REQUIRED_DEMO_DEVICE_COUNT,
                        isSending = sendingIds.contains(device.id),
                        healthLoopEnabled = healthEnabledIds.contains(device.id),
                        isBusy = isNetworkBusy,
                        clapMessage = clapMessages[device.id],
                        healthMessage = healthMessages[device.id],
                        onDemoSelectedChange = { setDemoSelected(device, it) },
                        onSend = { sendDeviceEvent(device) },
                        onHealthLoopChange = { setHealthLoop(device, it) },
                    )
                }
            }
        }
    }
}

@Composable
private fun TdoaControls(
    eastWestUs: Float,
    northSouthUs: Float,
    onEastWestChange: (Float) -> Unit,
    onNorthSouthChange: (Float) -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Text("Source", style = MaterialTheme.typography.titleSmall)
            TdoaSlider(
                label = "W/E",
                valueUs = eastWestUs,
                startLabel = "W",
                endLabel = "E",
                onValueChange = onEastWestChange,
            )
            TdoaSlider(
                label = "S/N",
                valueUs = northSouthUs,
                startLabel = "S",
                endLabel = "N",
                onValueChange = onNorthSouthChange,
            )
        }
    }
}

@Composable
private fun TdoaSlider(
    label: String,
    valueUs: Float,
    startLabel: String,
    endLabel: String,
    onValueChange: (Float) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text("$label: ${valueUs.roundToInt()} us")
        Slider(
            value = valueUs,
            onValueChange = { onValueChange(snapTdoaUs(it)) },
            valueRange = -TDOA_LIMIT_US..TDOA_LIMIT_US,
            steps = TDOA_SLIDER_STEPS,
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(startLabel)
            Text("0")
            Text(endLabel)
        }
    }
}

@Composable
private fun DeviceRow(
    device: DeviceDto,
    isDemoSelected: Boolean,
    canSelectForDemo: Boolean,
    isSending: Boolean,
    healthLoopEnabled: Boolean,
    isBusy: Boolean,
    clapMessage: String?,
    healthMessage: String?,
    onDemoSelectedChange: (Boolean) -> Unit,
    onSend: () -> Unit,
    onHealthLoopChange: (Boolean) -> Unit,
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(device.name.ifBlank { "Unnamed device" }, style = MaterialTheme.typography.titleMedium)
            Text("ID: ${device.id}", fontFamily = FontFamily.Monospace)
            Text("Tag: ${device.tag ?: "-"}")
            Text("Location: ${device.lat ?: "?"}, ${device.lon ?: "?"}")

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Checkbox(
                    checked = isDemoSelected,
                    onCheckedChange = onDemoSelectedChange,
                    enabled = canSelectForDemo,
                )
                Text("Demo")

                Switch(
                    checked = healthLoopEnabled,
                    onCheckedChange = onHealthLoopChange,
                )
                Text("Run")
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = onSend,
                    enabled = !isBusy,
                ) {
                    Text(if (isSending) "Sending..." else "Clap")
                }

                if (isSending) {
                    CircularProgressIndicator()
                }
            }

            if (!clapMessage.isNullOrBlank()) {
                Text("Clap: $clapMessage")
            }

            if (!healthMessage.isNullOrBlank()) {
                Text("Health: $healthMessage")
            }
        }
    }
}

private data class DeviceDto(
    val id: String,
    val name: String,
    val tag: String?,
    val lat: Double?,
    val lon: Double?,
)

private data class SoundEventDto(
    val id: Long,
)

private data class AudioPayload(
    val name: String,
    val bytes: ByteArray,
    val contentType: String,
)

private data class SendResult(
    val eventId: Long,
    val audioName: String?,
)

private fun sendEventWithAudio(
    apiClient: BackendApiClient,
    audioProvider: RawAudioProvider,
    baseUrl: String,
    device: DeviceDto,
    eventTimeNs: Long,
): SendResult {
    val audio = audioProvider.randomAudio()
    val event = apiClient.createSoundEvent(
        baseUrl = baseUrl,
        deviceId = device.id,
        eventTimeNs = eventTimeNs,
    )
    if (audio != null) {
        apiClient.uploadAudio(baseUrl, device.id, event.id, audio)
    }
    return SendResult(eventId = event.id, audioName = audio?.name)
}

private fun currentEpochNs(): Long = System.currentTimeMillis() * 1_000_000L

private fun snapTdoaUs(value: Float): Float =
    (value / TDOA_STEP_US).roundToInt()
        .times(TDOA_STEP_US)
        .coerceIn(-TDOA_LIMIT_US, TDOA_LIMIT_US)

private fun calculateArrivalOffsetsNs(
    devices: List<DeviceDto>,
    eastWestUs: Float,
    northSouthUs: Float,
): Map<String, Long> {
    val locatedDevices = devices.filter { it.lat != null && it.lon != null }
    if (locatedDevices.size < 2) {
        return devices.associate { it.id to 0L }
    }

    val minLat = locatedDevices.minOf { it.lat!! }
    val maxLat = locatedDevices.maxOf { it.lat!! }
    val minLon = locatedDevices.minOf { it.lon!! }
    val maxLon = locatedDevices.maxOf { it.lon!! }
    val centerLat = (minLat + maxLat) / 2.0
    val centerLon = (minLon + maxLon) / 2.0
    val latHalfRange = ((maxLat - minLat) / 2.0).takeIf { it > 0.0 } ?: 1.0
    val lonHalfRange = ((maxLon - minLon) / 2.0).takeIf { it > 0.0 } ?: 1.0

    return devices.associate { device ->
        val lat = device.lat
        val lon = device.lon
        if (lat == null || lon == null) {
            device.id to 0L
        } else {
            val northNorm = ((lat - centerLat) / latHalfRange).coerceIn(-1.0, 1.0)
            val eastNorm = ((lon - centerLon) / lonHalfRange).coerceIn(-1.0, 1.0)
            val offsetUs = -(eastWestUs.toDouble() * eastNorm + northSouthUs.toDouble() * northNorm)
            device.id to (offsetUs * 1_000.0).roundToLong()
        }
    }
}

private class BackendApiClient {
    fun listDevices(baseUrl: String): List<DeviceDto> {
        val response = request(
            url = apiUrl(baseUrl, "devices/"),
            method = "GET",
        )
        val array = JSONArray(response)
        return buildList {
            for (index in 0 until array.length()) {
                val item = array.getJSONObject(index)
                add(
                    DeviceDto(
                        id = item.getString("id"),
                        name = item.optString("name", ""),
                        tag = item.optNullableString("tag"),
                        lat = item.optNullableDouble("lat"),
                        lon = item.optNullableDouble("lon"),
                    )
                )
            }
        }
    }

    fun createSoundEvent(baseUrl: String, deviceId: String, eventTimeNs: Long): SoundEventDto {
        val peakLevel = Random.nextDouble(0.78, 0.99)
        val rmsLevel = Random.nextDouble(0.30, 0.56)
        val noiseFloor = Random.nextDouble(0.035, 0.14)
        val payload = JSONObject()
            .put("event_time_ns", eventTimeNs)
            .put("duration_ms", 10000)
            .put("pre_event_ms", Random.nextInt(4800, 5201))
            .put("post_event_ms", Random.nextInt(4800, 5201))
            .put("sample_rate_hz", 16000)
            .put("channels", 1)
            .put("sample_format", "pcm16le")
            .put("peak_level", peakLevel.round(4))
            .put("rms_level", rmsLevel.round(4))
            .put("noise_floor", noiseFloor.round(4))
            .put("detection_label", "impulse")

        val response = request(
            url = apiUrl(baseUrl, "devices/${pathSegment(deviceId)}/sound-events"),
            method = "POST",
            body = payload.toString().toByteArray(StandardCharsets.UTF_8),
            contentType = "application/json",
        )
        return SoundEventDto(JSONObject(response).getLong("id"))
    }

    fun uploadAudio(baseUrl: String, deviceId: String, eventId: Long, audio: AudioPayload) {
        request(
            url = apiUrl(baseUrl, "devices/${pathSegment(deviceId)}/sound-events/$eventId/audio"),
            method = "POST",
            body = audio.bytes,
            contentType = audio.contentType,
        )
    }

    fun syncTime(baseUrl: String, deviceId: String, clientSentMonotonicNs: Long) {
        request(
            url = apiUrl(
                baseUrl,
                "devices/${pathSegment(deviceId)}/time/sync?client_sent_monotonic_ns=$clientSentMonotonicNs",
            ),
            method = "GET",
        )
    }

    fun sendHealth(baseUrl: String, deviceId: String, payload: JSONObject) {
        request(
            url = apiUrl(baseUrl, "devices/${pathSegment(deviceId)}/health"),
            method = "POST",
            body = payload.toString().toByteArray(StandardCharsets.UTF_8),
            contentType = "application/json",
        )
    }

    private fun request(
        url: URL,
        method: String,
        body: ByteArray? = null,
        contentType: String? = null,
    ): String {
        val connection = (url.openConnection() as HttpURLConnection).apply {
            requestMethod = method
            connectTimeout = 10_000
            readTimeout = 20_000
            setRequestProperty("Accept", "application/json")
            if (body != null) {
                doOutput = true
                setRequestProperty("Content-Type", contentType ?: "application/octet-stream")
                setRequestProperty("Content-Length", body.size.toString())
            }
        }

        try {
            if (body != null) {
                connection.outputStream.use { it.write(body) }
            }

            val responseCode = connection.responseCode
            val responseText = readResponse(connection, responseCode)
            if (responseCode !in 200..299) {
                throw IllegalStateException("HTTP $responseCode: $responseText")
            }
            return responseText
        } finally {
            connection.disconnect()
        }
    }

    private fun readResponse(connection: HttpURLConnection, responseCode: Int): String {
        val stream = if (responseCode in 200..299) {
            connection.inputStream
        } else {
            connection.errorStream ?: connection.inputStream
        }
        return stream.bufferedReader(StandardCharsets.UTF_8).use { it.readText() }
    }

    private fun apiUrl(baseUrl: String, path: String): URL {
        val trimmed = baseUrl.trim().trimEnd('/')
        val apiBase = if (trimmed.endsWith("/api")) trimmed else "$trimmed/api"
        return URL("$apiBase/${path.trimStart('/')}")
    }

    private fun pathSegment(value: String): String =
        URLEncoder.encode(value, StandardCharsets.UTF_8.name()).replace("+", "%20")
}

private class HealthPayloadFactory {
    private val deviceStates = mutableMapOf<String, DeviceHealthState>()

    fun next(deviceId: String): JSONObject {
        val state = deviceStates.getOrPut(deviceId) {
            DeviceHealthState(
                bootEpochMs = System.currentTimeMillis() - Random.nextLong(2_000_000L, 86_400_000L),
                droppedChunks = Random.nextInt(0, 3),
            )
        }

        if (Random.nextDouble() < 0.10) {
            state.droppedChunks += Random.nextInt(1, 4)
        }

        val wifiConnected = Random.nextDouble() > 0.035
        val microphoneActive = Random.nextDouble() > 0.025
        val ina219Online = Random.nextDouble() > 0.02
        val busVoltage = Random.nextDouble(3.66, 4.16)
        val shuntVoltage = Random.nextDouble(0.35, 8.40)
        val current = Random.nextDouble(68.0, 188.0)
        val computedPower = busVoltage * current
        val reportedPower = computedPower * Random.nextDouble(0.96, 1.04)
        val queueDepth = if (Random.nextDouble() < 0.82) {
            Random.nextInt(0, 4)
        } else {
            Random.nextInt(4, 12)
        }
        val statusMessage = when {
            !wifiConnected -> "wifi reconnecting"
            !microphoneActive -> "mic warmup"
            queueDepth > 7 -> "buffer lag"
            Random.nextDouble() < 0.12 -> "rssi unstable"
            else -> "ok"
        }

        return JSONObject()
            .put("firmware_version", "edge-fw-1.4.${Random.nextInt(2, 7)}")
            .put("status_message", statusMessage)
            .put("uptime_ms", (System.currentTimeMillis() - state.bootEpochMs).coerceAtLeast(0L))
            .put("wifi_connected", wifiConnected)
            .put("microphone_active", microphoneActive)
            .put("ina219_online", ina219Online)
            .put("bus_voltage_v", busVoltage.round(3))
            .put("shunt_voltage_mv", shuntVoltage.round(3))
            .put("current_ma", current.round(2))
            .put("power_mw", reportedPower.round(2))
            .put("computed_power_mw", computedPower.round(2))
            .put("audio_queue_depth", queueDepth)
            .put("audio_dropped_chunks", state.droppedChunks)
    }

    private data class DeviceHealthState(
        val bootEpochMs: Long,
        var droppedChunks: Int,
    )
}

private class RawAudioProvider(private val context: Context) {
    fun randomAudio(): AudioPayload? {
        val resources = rawResources()
        if (resources.isEmpty()) {
            return null
        }

        val chosen = resources.random(Random(System.nanoTime()))
        val bytes = context.resources.openRawResource(chosen.id).use { input ->
            BufferedInputStream(input).use { buffered ->
                val output = ByteArrayOutputStream()
                val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                while (true) {
                    val read = buffered.read(buffer)
                    if (read == -1) {
                        break
                    }
                    output.write(buffer, 0, read)
                }
                output.toByteArray()
            }
        }

        return AudioPayload(
            name = chosen.name,
            bytes = bytes,
            contentType = if (bytes.isWav()) "audio/wav" else "application/octet-stream",
        )
    }

    private fun rawResources(): List<RawResource> {
        return try {
            val rawClass = Class.forName("${context.packageName}.R\$raw")
            rawClass.fields.mapNotNull { field ->
                val resourceId = field.getInt(null)
                RawResource(name = field.name, id = resourceId)
            }
        } catch (_: ClassNotFoundException) {
            emptyList()
        }
    }

    private fun ByteArray.isWav(): Boolean {
        return size >= 12 &&
            this[0] == 'R'.code.toByte() &&
            this[1] == 'I'.code.toByte() &&
            this[2] == 'F'.code.toByte() &&
            this[3] == 'F'.code.toByte() &&
            this[8] == 'W'.code.toByte() &&
            this[9] == 'A'.code.toByte() &&
            this[10] == 'V'.code.toByte() &&
            this[11] == 'E'.code.toByte()
    }

    private data class RawResource(
        val name: String,
        val id: Int,
    )
}

private fun JSONObject.optNullableString(name: String): String? {
    if (isNull(name)) {
        return null
    }
    return optString(name).takeIf { it.isNotBlank() }
}

private fun JSONObject.optNullableDouble(name: String): Double? {
    if (isNull(name)) {
        return null
    }
    val value = optDouble(name, Double.NaN)
    return value.takeIf { !it.isNaN() }
}

private fun Double.round(decimals: Int): Double {
    var multiplier = 1.0
    repeat(decimals.coerceAtLeast(0)) {
        multiplier *= 10.0
    }
    return (this * multiplier).roundToInt() / multiplier
}
