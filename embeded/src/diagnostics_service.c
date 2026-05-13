#include "diagnostics_service.h"

#include <stdio.h>
#include <string.h>

#define DIAGNOSTICS_LOG_INTERVAL_MS 5000

void diagnostics_service_init(diagnostics_service_t *service, bool enabled)
{
    if (service == NULL)
    {
        return;
    }

    memset(service, 0, sizeof(*service));
    service->enabled = enabled;
    service->next_log_at = get_absolute_time();
}

void diagnostics_service_poll(diagnostics_service_t *service,
                              const device_status_snapshot_t *snapshot,
                              const power_meter_service_t *power_meter_service,
                              uint16_t audio_queue_depth,
                              uint32_t audio_dropped_chunks,
                              uint16_t dsp_frame_depth,
                              uint32_t dsp_dropped_frames,
                              uint32_t processed_capture_buffers)
{
    if (service == NULL || snapshot == NULL || !service->enabled)
    {
        return;
    }

    if (!time_reached(service->next_log_at))
    {
        return;
    }

    const char *cmsis_pipeline_state = snapshot->microphone == DEVICE_COMPONENT_OK
                                           ? (processed_capture_buffers > 0u ? "Ready" : "Priming")
                                           : "Pending";

    printf("[Diagnostics] Microphone=%s | INA219=%s | SD Card=%s | Wi-Fi=%s | Server=%s | UDP=%s | CMSIS-DSP=%s\n",
           device_component_state_label(snapshot->microphone),
           device_component_state_label(snapshot->ina219),
           device_component_state_label(snapshot->sd_card),
           device_wifi_state_label(snapshot->wifi),
           device_server_state_label(snapshot->server),
           snapshot->udp_ready ? "Ready" : "Not Ready",
           cmsis_pipeline_state);

    ina219_wattmeter_reading_t reading;
    if (power_meter_service != NULL && power_meter_service_get_latest_reading(power_meter_service, &reading))
    {
        printf("[Diagnostics] Power Bus=%.3fV | Current=%.1fmA | Power=%.1fmW | Audio Queue=%u | Dropped=%lu | DSP Frames=%u | DSP Dropped=%lu | Buffers=%lu\n",
               reading.bus_voltage_v,
               reading.current_ma,
               reading.power_mw,
               (unsigned int)audio_queue_depth,
               (unsigned long)audio_dropped_chunks,
               (unsigned int)dsp_frame_depth,
               (unsigned long)dsp_dropped_frames,
               (unsigned long)processed_capture_buffers);
    }
    else
    {
        printf("[Diagnostics] Audio Queue=%u | Dropped=%lu | DSP Frames=%u | DSP Dropped=%lu | Buffers=%lu\n",
               (unsigned int)audio_queue_depth,
               (unsigned long)audio_dropped_chunks,
               (unsigned int)dsp_frame_depth,
               (unsigned long)dsp_dropped_frames,
               (unsigned long)processed_capture_buffers);
    }

    service->next_log_at = make_timeout_time_ms(DIAGNOSTICS_LOG_INTERVAL_MS);
}
