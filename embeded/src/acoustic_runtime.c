#include "acoustic_runtime.h"

#include <stddef.h>
#include <string.h>

#include "audio_stream_queue.h"
#include "device_clock.h"
#include "device_runtime_config.h"
#include "microphone.h"
#include "network_runtime.h"
#include "pico/stdlib.h"

static void on_microphone_buffer_ready(const int32_t *buffer, size_t words, uint64_t buffer_ready_us)
{
    audio_stream_queue_push_from_buffer(buffer, words, buffer_ready_us);
}

static void upload_sound_event(const device_config_t *config, const acoustic_runtime_t *runtime)
{
    (void)config;
    (void)runtime;
}

void acoustic_runtime_init(acoustic_runtime_t *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    audio_stream_queue_init();
    sound_event_detector_init(&runtime->detector);
    microphone_set_buffer_callback(on_microphone_buffer_ready);
    microphone_init();
}

bool acoustic_runtime_poll(acoustic_runtime_t *runtime,
                           const device_config_t *config,
                           diagnostics_service_t *diagnostics_service,
                           power_meter_service_t *power_meter_service,
                           device_status_snapshot_t *status,
                           device_wifi_state_t *wifi_state,
                           bool microphone_ready)
{
    if (runtime == NULL)
    {
        return false;
    }

    bool processed_any = false;
    for (int burst = 0; burst < 8; ++burst)
    {
        audio_chunk_view_t chunk;
        if (!audio_stream_queue_peek(&chunk) || chunk.sample_count == 0)
        {
            break;
        }

        sound_event_detector_result_t detection = {0};
        if (!runtime->pending_upload &&
            sound_event_detector_process_chunk(&runtime->detector, chunk.samples, chunk.sample_count, &detection))
        {
            runtime->pending_upload = true;
            runtime->event_time_ns = device_clock_now_utc_ns();
            runtime->peak_abs = detection.peak_abs;
            runtime->mean_abs = detection.mean_abs;
            runtime->noise_floor_abs = detection.noise_floor_abs;
            runtime->upload_after = make_timeout_time_ms(DEVICE_SOUND_EVENT_POST_CAPTURE_MS);
        }

        audio_stream_queue_pop_if_matches(chunk.slot);
        processed_any = true;
    }

    if (runtime->pending_upload && time_reached(runtime->upload_after))
    {
        network_runtime_connect_wifi_with_retries(config,
                                                  diagnostics_service,
                                                  power_meter_service,
                                                  NULL,
                                                  status,
                                                  wifi_state,
                                                  microphone_ready);
        upload_sound_event(config, runtime);
        runtime->pending_upload = false;
        network_runtime_sleep_wifi(config,
                                   diagnostics_service,
                                   power_meter_service,
                                   status,
                                   wifi_state,
                                   microphone_ready);
    }

    return processed_any;
}
