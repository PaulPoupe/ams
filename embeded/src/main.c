#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "audio_stream_queue.h"
#include "device_config.h"
#include "device_clock.h"
#include "device_status.h"
#include "diagnostics_service.h"
#include "inky_status_display.h"
#include "microphone.h"
#include "power_meter_service.h"
#include "sd_card_buffer.h"
#include "server_health.h"
#include "startup_helpers.h"
#include "time_sync_service.h"
#include "udp_service.h"
#include "wifi_service.h"

#define SERVER_PORT 5000
#define USB_CONSOLE_DETECT_WINDOW_MS 7000
#define RUNTIME_POLL_SLEEP_MS 50

_Static_assert(AUDIO_STREAM_QUEUE_OUTPUT_SAMPLE_RATE_HZ == SD_CARD_AUDIO_SAMPLE_RATE_HZ,
               "SD audio buffer duration assumes the current audio output sample rate");

static diagnostics_service_t g_diagnostics_service;
static sd_card_buffer_t g_sd_card_buffer;

static void publish_runtime_state(const device_config_t *config,
                                  power_meter_service_t *power_meter_service,
                                  const server_health_service_t *server_health_service,
                                  device_status_snapshot_t *status,
                                  device_wifi_state_t wifi_state,
                                  bool microphone_ready);

static void on_microphone_buffer_ready(const int32_t *buffer, size_t words, uint64_t buffer_ready_us)
{
    audio_stream_queue_push_from_buffer(buffer, words, buffer_ready_us);
}

static bool wait_for_usb_console(power_meter_service_t *power_meter_service)
{
    absolute_time_t deadline = make_timeout_time_ms(USB_CONSOLE_DETECT_WINDOW_MS);
    while (!time_reached(deadline))
    {
        if (stdio_usb_connected())
        {
            return true;
        }

        power_meter_service_poll(power_meter_service);
        sleep_ms(RUNTIME_POLL_SLEEP_MS);
    }

    return stdio_usb_connected();
}

static device_server_state_t map_server_status(const server_health_service_t *server_health_service,
                                               device_wifi_state_t wifi_state)
{
    if (server_health_service == NULL)
    {
        return DEVICE_SERVER_PENDING;
    }

    if (wifi_state == DEVICE_WIFI_SLEEP || wifi_state == DEVICE_WIFI_CONNECTING)
    {
        return DEVICE_SERVER_PENDING;
    }

    if (wifi_state == DEVICE_WIFI_ERROR)
    {
        return DEVICE_SERVER_ERROR;
    }

    switch (server_health_service_connection_status(server_health_service))
    {
    case SERVER_HEALTH_STATUS_CONNECTED:
        return DEVICE_SERVER_CONNECTED;
    case SERVER_HEALTH_STATUS_ERROR:
        return DEVICE_SERVER_ERROR;
    case SERVER_HEALTH_STATUS_PENDING:
    default:
        return DEVICE_SERVER_PENDING;
    }
}

static device_component_state_t map_sd_card_status(void)
{
    if (!sd_card_buffer_is_initialized(&g_sd_card_buffer))
    {
        return DEVICE_COMPONENT_PENDING;
    }

    return sd_card_buffer_is_ready(&g_sd_card_buffer) ? DEVICE_COMPONENT_OK : DEVICE_COMPONENT_ERROR;
}

static void refresh_runtime_status(device_status_snapshot_t *status,
                                   const power_meter_service_t *power_meter_service,
                                   const server_health_service_t *server_health_service,
                                   device_wifi_state_t wifi_state,
                                   bool microphone_ready)
{
    if (status == NULL)
    {
        return;
    }

    status->microphone = microphone_ready ? DEVICE_COMPONENT_OK : DEVICE_COMPONENT_PENDING;
    status->ina219 = power_meter_service_is_sensor_online(power_meter_service) ? DEVICE_COMPONENT_OK : DEVICE_COMPONENT_ERROR;
    status->sd_card = map_sd_card_status();
    status->wifi = wifi_state;
    status->server = map_server_status(server_health_service, wifi_state);
    status->udp_ready = is_udp_ready();
}

static void run_runtime_iteration(const device_config_t *config,
                                  power_meter_service_t *power_meter_service,
                                  time_sync_service_t *time_sync_service,
                                  server_health_service_t *server_health_service,
                                  device_status_snapshot_t *status,
                                  device_wifi_state_t wifi_state,
                                  bool microphone_ready,
                                  bool wifi_connected,
                                  bool udp_connected,
                                  bool report_server_health)
{
    power_meter_service_poll(power_meter_service);
    if (time_sync_service != NULL)
    {
        time_sync_service_poll(time_sync_service);
    }

    if (report_server_health && server_health_service != NULL)
    {
        server_health_service_poll(server_health_service,
                                   power_meter_service,
                                   wifi_connected,
                                   udp_connected,
                                   audio_stream_queue_depth(),
                                   audio_stream_queue_dropped_chunks());
    }

    publish_runtime_state(config,
                          power_meter_service,
                          server_health_service,
                          status,
                          wifi_state,
                          microphone_ready);
}

static void publish_runtime_state(const device_config_t *config,
                                  power_meter_service_t *power_meter_service,
                                  const server_health_service_t *server_health_service,
                                  device_status_snapshot_t *status,
                                  device_wifi_state_t wifi_state,
                                  bool microphone_ready)
{
    if (config == NULL || status == NULL)
    {
        return;
    }

    refresh_runtime_status(status, power_meter_service, server_health_service, wifi_state, microphone_ready);
    diagnostics_service_poll(&g_diagnostics_service,
                             status,
                             power_meter_service,
                             audio_stream_queue_depth(),
                             audio_stream_queue_dropped_chunks(),
                             audio_dsp_frame_depth(),
                             audio_dsp_frame_dropped(),
                             audio_stream_queue_processed_buffers());
    inky_status_display_update(config,
                               status,
                               AUDIO_STREAM_QUEUE_INPUT_SAMPLE_RATE_HZ,
                               AUDIO_STREAM_QUEUE_DOWNSAMPLE_FACTOR);
}

static void set_wifi_state_and_publish(const device_config_t *config,
                                       power_meter_service_t *power_meter_service,
                                       const server_health_service_t *server_health_service,
                                       device_status_snapshot_t *status,
                                       device_wifi_state_t *wifi_state,
                                       device_wifi_state_t next_state,
                                       bool microphone_ready)
{
    if (wifi_state == NULL)
    {
        return;
    }

    *wifi_state = next_state;
    publish_runtime_state(config,
                          power_meter_service,
                          server_health_service,
                          status,
                          *wifi_state,
                          microphone_ready);
}

static void sleep_with_runtime_polling(uint32_t sleep_ms_total,
                                       const device_config_t *config,
                                       power_meter_service_t *power_meter_service,
                                       time_sync_service_t *time_sync_service,
                                       const server_health_service_t *server_health_service,
                                       device_status_snapshot_t *status,
                                       device_wifi_state_t wifi_state,
                                       bool microphone_ready)
{
    absolute_time_t deadline = make_timeout_time_ms(sleep_ms_total);
    while (!time_reached(deadline))
    {
        run_runtime_iteration(config,
                              power_meter_service,
                              time_sync_service,
                              (server_health_service_t *)server_health_service,
                              status,
                              wifi_state,
                              microphone_ready,
                              false,
                              false,
                              false);
        sleep_ms(RUNTIME_POLL_SLEEP_MS);
    }
}

static void connect_wifi_with_retries(const device_config_t *config,
                                      power_meter_service_t *power_meter_service,
                                      const server_health_service_t *server_health_service,
                                      device_status_snapshot_t *status,
                                      device_wifi_state_t *wifi_state,
                                      bool microphone_ready)
{
    if (config == NULL || power_meter_service == NULL || status == NULL || wifi_state == NULL)
    {
        return;
    }

    set_wifi_state_and_publish(config,
                               power_meter_service,
                               server_health_service,
                               status,
                               wifi_state,
                               DEVICE_WIFI_CONNECTING,
                               microphone_ready);

    while (!is_wifi_connected())
    {
        if (connect_to_wifi(config->ssid, config->password) == 0)
        {
            set_wifi_state_and_publish(config,
                                       power_meter_service,
                                       server_health_service,
                                       status,
                                       wifi_state,
                                       DEVICE_WIFI_CONNECTED,
                                       microphone_ready);
            return;
        }

        set_wifi_state_and_publish(config,
                                   power_meter_service,
                                   server_health_service,
                                   status,
                                   wifi_state,
                                   DEVICE_WIFI_ERROR,
                                   microphone_ready);
        printf("Retrying Wi-Fi connection...\n");
        sleep_with_runtime_polling(5000,
                                   config,
                                   power_meter_service,
                                   NULL,
                                   server_health_service,
                                   status,
                                   *wifi_state,
                                   microphone_ready);
        set_wifi_state_and_publish(config,
                                   power_meter_service,
                                   server_health_service,
                                   status,
                                   wifi_state,
                                   DEVICE_WIFI_CONNECTING,
                                   microphone_ready);
    }

    *wifi_state = DEVICE_WIFI_CONNECTED;
}

static bool ensure_wifi_connected(const char *disconnect_message,
                                  const char *reconnected_message,
                                  const device_config_t *config,
                                  power_meter_service_t *power_meter_service,
                                  server_health_service_t *server_health_service,
                                  device_status_snapshot_t *status,
                                  device_wifi_state_t *wifi_state,
                                  bool microphone_ready)
{
    if (is_wifi_connected())
    {
        *wifi_state = DEVICE_WIFI_CONNECTED;
        return false;
    }

    set_wifi_state_and_publish(config,
                               power_meter_service,
                               server_health_service,
                               status,
                               wifi_state,
                               DEVICE_WIFI_ERROR,
                               microphone_ready);
    printf("%s\n", disconnect_message);
    connect_wifi_with_retries(config,
                              power_meter_service,
                              server_health_service,
                              status,
                              wifi_state,
                              microphone_ready);
    printf("%s\n", reconnected_message);
    return true;
}

static void wait_for_time_sync(const device_config_t *config,
                               power_meter_service_t *power_meter_service,
                               time_sync_service_t *time_sync_service,
                               device_status_snapshot_t *status,
                               device_wifi_state_t *wifi_state,
                               bool microphone_ready)
{
    time_sync_service_request_sync(time_sync_service);
    while (!time_sync_service_is_synced(time_sync_service))
    {
        if (ensure_wifi_connected("Wi-Fi lost during time sync. Reconnecting...",
                                  "Wi-Fi reconnected",
                                  config,
                                  power_meter_service,
                                  NULL,
                                  status,
                                  wifi_state,
                                  microphone_ready))
        {
            time_sync_service_request_sync(time_sync_service);
        }

        run_runtime_iteration(config,
                              power_meter_service,
                              time_sync_service,
                              NULL,
                              status,
                              *wifi_state,
                              microphone_ready,
                              true,
                              false,
                              false);
        sleep_ms(RUNTIME_POLL_SLEEP_MS);
    }

    printf("Clock synced: utc=%llu local=%llu rtt=%llu ns tz=%s offset=%ld sec\n",
           (unsigned long long)device_clock_now_utc_ns(),
           (unsigned long long)device_clock_now_local_ns(),
           (unsigned long long)device_clock_last_rtt_ns(),
           device_clock_timezone_name(),
           (long)device_clock_timezone_offset_seconds());
}

int main()
{
    stdio_init_all();
    sleep_ms(250);

    power_meter_service_t power_meter_service;
    power_meter_service_init(&power_meter_service);

    bool usb_console_connected = wait_for_usb_console(&power_meter_service);
    printf("USB console %s\n", usb_console_connected ? "detected" : "not detected");

    device_config_t config;
    bool has_config = device_config_load(&config);
    startup_run_mode_t run_mode = resolve_startup_config(&config, has_config, usb_console_connected);
    diagnostics_service_init(&g_diagnostics_service, run_mode == STARTUP_RUN_MODE_DIAGNOSTICS);
    printf("Selected startup mode: %s\n", startup_run_mode_label(run_mode));

    device_status_snapshot_t runtime_status;
    device_status_snapshot_init(&runtime_status);
    inky_status_display_reset();

    print_current_settings(&config,
                           SERVER_PORT,
                           AUDIO_STREAM_QUEUE_INPUT_SAMPLE_RATE_HZ,
                           AUDIO_STREAM_QUEUE_DOWNSAMPLE_FACTOR);

    device_wifi_state_t wifi_state = DEVICE_WIFI_SLEEP;
    bool microphone_ready = false;
    publish_runtime_state(&config, &power_meter_service, NULL, &runtime_status, wifi_state, microphone_ready);
    sd_card_buffer_init(&g_sd_card_buffer);
    publish_runtime_state(&config, &power_meter_service, NULL, &runtime_status, wifi_state, microphone_ready);

    if (initialize_network() != 0)
    {
        wifi_state = DEVICE_WIFI_ERROR;
        publish_runtime_state(&config, &power_meter_service, NULL, &runtime_status, wifi_state, microphone_ready);
        printf("Network initialization failed, stopping startup.\n");
        return 1;
    }

    connect_wifi_with_retries(&config,
                              &power_meter_service,
                              NULL,
                              &runtime_status,
                              &wifi_state,
                              microphone_ready);
    printf("Wi-Fi connected\n");

    device_clock_reset();
    time_sync_service_t time_sync_service;
    time_sync_service_init(&time_sync_service, &config);
    wait_for_time_sync(&config,
                       &power_meter_service,
                       &time_sync_service,
                       &runtime_status,
                       &wifi_state,
                       microphone_ready);

    server_health_service_t server_health_service;
    server_health_service_init(&server_health_service, &config);
    publish_runtime_state(&config,
                          &power_meter_service,
                          &server_health_service,
                          &runtime_status,
                          wifi_state,
                          microphone_ready);

    while (udp_setup_endpoint(config.server_ip, SERVER_PORT) != 0)
    {
        run_runtime_iteration(&config,
                              &power_meter_service,
                              &time_sync_service,
                              &server_health_service,
                              &runtime_status,
                              wifi_state,
                              microphone_ready,
                              true,
                              false,
                              false);
        printf("Retrying UDP endpoint setup...\n");
        sleep_with_runtime_polling(5000,
                                   &config,
                                   &power_meter_service,
                                   &time_sync_service,
                                   &server_health_service,
                                   &runtime_status,
                                   wifi_state,
                                   microphone_ready);
    }
    printf("UDP endpoint ready (mono16 ~16kHz)\n");

    audio_stream_queue_init();
    microphone_set_buffer_callback(on_microphone_buffer_ready);
    microphone_init();
    microphone_ready = true;
    server_health_service_set_microphone_active(&server_health_service, true);
    publish_runtime_state(&config,
                          &power_meter_service,
                          &server_health_service,
                          &runtime_status,
                          wifi_state,
                          microphone_ready);
    printf("Microphone enabled\n");

    while (true)
    {
        if (ensure_wifi_connected("Wi-Fi disconnected. Reconnecting...",
                                  "Wi-Fi reconnected",
                                  &config,
                                  &power_meter_service,
                                  &server_health_service,
                                  &runtime_status,
                                  &wifi_state,
                                  microphone_ready))
        {
            time_sync_service_request_sync(&time_sync_service);
        }

        if (!is_udp_ready())
        {
            if (udp_setup_endpoint(config.server_ip, SERVER_PORT) == 0)
            {
                printf("UDP endpoint restored\n");
            }
            else
            {
                run_runtime_iteration(&config,
                                      &power_meter_service,
                                      &time_sync_service,
                                      &server_health_service,
                                      &runtime_status,
                                      wifi_state,
                                      microphone_ready,
                                      true,
                                      false,
                                      true);
                sleep_ms(250);
                continue;
            }
        }

        run_runtime_iteration(&config,
                              &power_meter_service,
                              &time_sync_service,
                              &server_health_service,
                              &runtime_status,
                              wifi_state,
                              microphone_ready,
                              true,
                              is_udp_ready(),
                              true);

        bool sent_any = false;
        for (int burst = 0; burst < 8; ++burst)
        {
            audio_chunk_view_t chunk;
            if (!audio_stream_queue_peek(&chunk) || chunk.sample_count == 0)
            {
                break;
            }

            if (!udp_send_audio_mono16(config.device_id, chunk.samples, chunk.sample_count))
            {
                break;
            }

            sd_card_buffer_write_audio(&g_sd_card_buffer, chunk.samples, chunk.sample_count);
            audio_stream_queue_pop_if_matches(chunk.slot);
            sent_any = true;
        }

        if (!sent_any)
        {
            sleep_ms(2);
        }
    }
}
