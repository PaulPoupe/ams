#include "inky_status_display.h"

#include <cstdio>
#include <cstring>

#include "pico/time.h"
#include "uc8151.hpp"
#include "pico_graphics.hpp"

namespace {

using namespace pimoroni;

constexpr uint16_t kDisplayWidth = 296;
constexpr uint16_t kDisplayHeight = 128;
constexpr size_t kFrameBufferSize = (kDisplayWidth * kDisplayHeight) / 8;

constexpr uint kDisplayCsPin = 17;
constexpr uint kDisplayClockPin = 18;
constexpr uint kDisplayMosiPin = 19;
constexpr uint kDisplayDcPin = 20;
constexpr uint kDisplayResetPin = 21;
constexpr uint kDisplayBusyPin = 26;

constexpr int kTextLeft = 4;
constexpr int kTextTop = 4;
constexpr int kLineStep = 12;
constexpr int kTextWrap = kDisplayWidth - (kTextLeft * 2);
constexpr uint32_t kDisplayRefreshIntervalMs = 60000;

struct display_context_t {
    uint8_t framebuffer[kFrameBufferSize];
    UC8151 display;
    PicoGraphics_Pen1BitY graphics;
    device_status_snapshot_t last_status;
    device_config_t last_config;
    absolute_time_t last_refresh_at;
    bool has_rendered;

    display_context_t()
        : framebuffer{},
          display(kDisplayWidth,
                  kDisplayHeight,
                  ROTATE_0,
                  {spi0, kDisplayCsPin, kDisplayClockPin, kDisplayMosiPin, PIN_UNUSED, kDisplayDcPin, PIN_UNUSED},
                  kDisplayBusyPin,
                  kDisplayResetPin),
          graphics(kDisplayWidth, kDisplayHeight, framebuffer),
          last_status{},
          last_config{},
          last_refresh_at{},
          has_rendered(false)
    {
        graphics.set_font("bitmap8");
        display.set_update_speed(1);
        device_status_snapshot_init(&last_status);
    }
};

display_context_t *get_display_context()
{
    static display_context_t context;
    return &context;
}

void draw_line(PicoGraphics_Pen1BitY &graphics, int line_index, const char *text)
{
    graphics.text(text, Point(kTextLeft, kTextTop + (line_index * kLineStep)), kTextWrap, 1.0f);
}

void format_line(char *out, size_t out_size, const char *label, const char *value)
{
    if (out == nullptr || out_size == 0)
    {
        return;
    }

    const char *safe_value = (value != nullptr && value[0] != '\0') ? value : "-";
    std::snprintf(out, out_size, "%s%.28s", label, safe_value);
    out[out_size - 1] = '\0';
}

void format_audio_line(char *out, size_t out_size, int raw_audio_sample_rate_hz, int audio_downsample_factor)
{
    std::snprintf(out, out_size, "Audio: %dHz/x%d", raw_audio_sample_rate_hz, audio_downsample_factor);
    out[out_size - 1] = '\0';
}

void format_status_line(char *out, size_t out_size, const char *label, const char *value)
{
    std::snprintf(out, out_size, "%s%s", label, value);
    out[out_size - 1] = '\0';
}

bool config_equals(const device_config_t *lhs, const device_config_t *rhs)
{
    return std::strncmp(lhs->ssid, rhs->ssid, sizeof(lhs->ssid)) == 0 &&
           std::strncmp(lhs->device_id, rhs->device_id, sizeof(lhs->device_id)) == 0;
}

bool display_status_equals(const device_status_snapshot_t *lhs, const device_status_snapshot_t *rhs)
{
    if (lhs == nullptr || rhs == nullptr)
    {
        return false;
    }

    device_status_snapshot_t normalized_lhs = *lhs;
    device_status_snapshot_t normalized_rhs = *rhs;
    normalized_lhs.udp_ready = false;
    normalized_rhs.udp_ready = false;
    return device_status_snapshot_equals(&normalized_lhs, &normalized_rhs);
}

void render_screen(display_context_t *context,
                   const device_config_t *config,
                   const device_status_snapshot_t *status,
                   int raw_audio_sample_rate_hz,
                   int audio_downsample_factor)
{
    PicoGraphics_Pen1BitY &graphics = context->graphics;
    char line_buffer[40];

    graphics.set_pen(15);
    graphics.clear();
    graphics.set_pen(0);

    draw_line(graphics, 0, "AMS status");
    format_line(line_buffer, sizeof(line_buffer), "ID: ", config != nullptr ? config->device_id : nullptr);
    draw_line(graphics, 1, line_buffer);
    format_line(line_buffer, sizeof(line_buffer), "WiFi: ", config != nullptr ? config->ssid : nullptr);
    draw_line(graphics, 2, line_buffer);
    format_audio_line(line_buffer, sizeof(line_buffer), raw_audio_sample_rate_hz, audio_downsample_factor);
    draw_line(graphics, 3, line_buffer);

    format_status_line(line_buffer, sizeof(line_buffer), "Mic: ", device_component_state_label(status->microphone));
    draw_line(graphics, 4, line_buffer);
    format_status_line(line_buffer, sizeof(line_buffer), "INA219: ", device_component_state_label(status->ina219));
    draw_line(graphics, 5, line_buffer);
    format_status_line(line_buffer, sizeof(line_buffer), "SD: ", device_component_state_label(status->sd_card));
    draw_line(graphics, 6, line_buffer);
    format_status_line(line_buffer, sizeof(line_buffer), "Wi-Fi: ", device_wifi_state_label(status->wifi));
    draw_line(graphics, 7, line_buffer);
    format_status_line(line_buffer, sizeof(line_buffer), "Server: ", device_server_state_label(status->server));
    draw_line(graphics, 8, line_buffer);

    context->display.update(&graphics);
}

} // namespace

extern "C" void inky_status_display_reset(void)
{
    display_context_t *context = get_display_context();
    context->has_rendered = false;
    device_status_snapshot_init(&context->last_status);
    std::memset(&context->last_config, 0, sizeof(context->last_config));
}

extern "C" void inky_status_display_update(const device_config_t *config,
                                           const device_status_snapshot_t *status,
                                           int raw_audio_sample_rate_hz,
                                           int audio_downsample_factor)
{
    if (config == nullptr || status == nullptr)
    {
        return;
    }

    display_context_t *context = get_display_context();
    bool config_changed = !context->has_rendered || !config_equals(config, &context->last_config);
    bool status_changed = !context->has_rendered || !display_status_equals(status, &context->last_status);

    if (!config_changed && !status_changed)
    {
        return;
    }

    absolute_time_t now = get_absolute_time();
    bool interval_elapsed = !context->has_rendered ||
                            absolute_time_diff_us(context->last_refresh_at, now) >=
                                ((int64_t)kDisplayRefreshIntervalMs * 1000ll);

    if (!config_changed && !interval_elapsed)
    {
        return;
    }

    render_screen(context, config, status, raw_audio_sample_rate_hz, audio_downsample_factor);
    context->last_status = *status;
    context->last_config = *config;
    context->last_refresh_at = now;
    context->has_rendered = true;
}
