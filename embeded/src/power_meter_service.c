#include "power_meter_service.h"

#include <stdio.h>
#include <string.h>

#define POWER_METER_PROBE_INTERVAL_MS 2000
#define POWER_METER_SAMPLE_INTERVAL_MS 1000

static void schedule_next_probe(power_meter_service_t *service)
{
    service->next_probe_at = make_timeout_time_ms(POWER_METER_PROBE_INTERVAL_MS);
}

static void schedule_next_sample(power_meter_service_t *service)
{
    service->next_sample_at = make_timeout_time_ms(POWER_METER_SAMPLE_INTERVAL_MS);
}

static void try_open_sensor(power_meter_service_t *service)
{
    if (ina219_wattmeter_init(&service->sensor, &service->sensor_config))
    {
        service->sensor_online = true;
        service->has_reading = false;
        service->offline_state_reported = false;
        service->next_sample_at = get_absolute_time();
        printf("Wattmeter ready on I2C1, address 0x%02X (SDA=GP6, SCL=GP7)\n",
               service->sensor.address);
        return;
    }

    service->sensor_online = false;
    service->has_reading = false;
    if (!service->offline_state_reported)
    {
        printf("Wattmeter probe failed: %s\n", ina219_wattmeter_last_error(&service->sensor));
        service->offline_state_reported = true;
    }
    schedule_next_probe(service);
}

void power_meter_service_init(power_meter_service_t *service)
{
    if (service == NULL)
    {
        return;
    }

    memset(service, 0, sizeof(*service));
    ina219_wattmeter_init_default_config(&service->sensor_config);

    printf("Starting DFRobot Gravity I2C Digital Wattmeter logger\n");
    printf("Expected Pico wiring: SDA=GP6, SCL=GP7, bus=I2C1\n");

    try_open_sensor(service);
}

void power_meter_service_poll(power_meter_service_t *service)
{
    if (service == NULL)
    {
        return;
    }

    if (!service->sensor_online)
    {
        if (time_reached(service->next_probe_at))
        {
            try_open_sensor(service);
        }
        return;
    }

    if (!time_reached(service->next_sample_at))
    {
        return;
    }

    ina219_wattmeter_reading_t reading;
    if (!ina219_wattmeter_read(&service->sensor, &reading))
    {
        printf("Wattmeter read failed: %s\n", ina219_wattmeter_last_error(&service->sensor));
        service->sensor_online = false;
        service->offline_state_reported = false;
        schedule_next_probe(service);
        return;
    }

    service->last_reading = reading;
    service->has_reading = true;

    schedule_next_sample(service);
}

bool power_meter_service_is_sensor_online(const power_meter_service_t *service)
{
    if (service == NULL)
    {
        return false;
    }

    return service->sensor_online;
}

bool power_meter_service_get_latest_reading(const power_meter_service_t *service, ina219_wattmeter_reading_t *reading)
{
    if (service == NULL || reading == NULL || !service->has_reading)
    {
        return false;
    }

    *reading = service->last_reading;
    return true;
}
