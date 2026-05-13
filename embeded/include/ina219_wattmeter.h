#ifndef INA219_WATTMETER_H
#define INA219_WATTMETER_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

typedef struct
{
    float bus_voltage_v;
    float shunt_voltage_mv;
    float current_ma;
    float power_mw;
    float computed_power_mw;
} ina219_wattmeter_reading_t;

typedef struct
{
    i2c_inst_t *i2c;
    uint8_t sda_pin;
    uint8_t scl_pin;
    uint32_t baudrate_hz;
    uint8_t address;
} ina219_wattmeter_config_t;

typedef struct
{
    i2c_inst_t *i2c;
    uint8_t address;
    uint16_t calibration_value;
    const char *last_error;
    bool initialized;
} ina219_wattmeter_t;

void ina219_wattmeter_init_default_config(ina219_wattmeter_config_t *config);
bool ina219_wattmeter_init(ina219_wattmeter_t *sensor, const ina219_wattmeter_config_t *config);
bool ina219_wattmeter_read(ina219_wattmeter_t *sensor, ina219_wattmeter_reading_t *reading);
bool ina219_wattmeter_linear_calibrate(ina219_wattmeter_t *sensor,
                                       float ina219_reading_ma,
                                       float actual_reading_ma);
const char *ina219_wattmeter_last_error(const ina219_wattmeter_t *sensor);

#endif
