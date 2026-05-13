#ifndef POWER_METER_SERVICE_H
#define POWER_METER_SERVICE_H

#include <stdbool.h>
#include "pico/time.h"
#include "ina219_wattmeter.h"

typedef struct
{
    ina219_wattmeter_t sensor;
    ina219_wattmeter_config_t sensor_config;
    ina219_wattmeter_reading_t last_reading;
    absolute_time_t next_probe_at;
    absolute_time_t next_sample_at;
    bool sensor_online;
    bool has_reading;
    bool offline_state_reported;
} power_meter_service_t;

void power_meter_service_init(power_meter_service_t *service);
void power_meter_service_poll(power_meter_service_t *service);
bool power_meter_service_is_sensor_online(const power_meter_service_t *service);
bool power_meter_service_get_latest_reading(const power_meter_service_t *service, ina219_wattmeter_reading_t *reading);

#endif
