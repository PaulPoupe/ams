#include "ina219_wattmeter.h"

#include <string.h>
#include "hardware/gpio.h"

#define INA219_REG_CONFIG 0x00u
#define INA219_REG_SHUNTVOLTAGE 0x01u
#define INA219_REG_BUSVOLTAGE 0x02u
#define INA219_REG_POWER 0x03u
#define INA219_REG_CURRENT 0x04u
#define INA219_REG_CALIBRATION 0x05u

#define INA219_DEFAULT_CONFIG 0x3D57u
#define INA219_DEFAULT_CALIBRATION 4096u
#define INA219_I2C_TIMEOUT_US 2000u

static const uint8_t INA219_KNOWN_ADDRESSES[] = {0x40u, 0x41u, 0x44u, 0x45u};

static void set_error(ina219_wattmeter_t *sensor, const char *message)
{
    if (sensor != NULL)
    {
        sensor->last_error = message;
    }
}

static bool write_register(ina219_wattmeter_t *sensor, uint8_t reg, uint16_t value)
{
    uint8_t payload[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFu)};

    int written = i2c_write_timeout_us(sensor->i2c,
                                       sensor->address,
                                       payload,
                                       sizeof(payload),
                                       false,
                                       INA219_I2C_TIMEOUT_US);
    if (written != (int)sizeof(payload))
    {
        set_error(sensor, "I2C write failed");
        return false;
    }

    return true;
}

static bool read_register(ina219_wattmeter_t *sensor, uint8_t reg, uint16_t *value)
{
    if (value == NULL)
    {
        set_error(sensor, "Register output pointer is null");
        return false;
    }

    int written = i2c_write_timeout_us(sensor->i2c,
                                       sensor->address,
                                       &reg,
                                       1,
                                       true,
                                       INA219_I2C_TIMEOUT_US);
    if (written != 1)
    {
        set_error(sensor, "I2C register select failed");
        return false;
    }

    uint8_t buffer[2] = {0};
    int read = i2c_read_timeout_us(sensor->i2c,
                                   sensor->address,
                                   buffer,
                                   sizeof(buffer),
                                   false,
                                   INA219_I2C_TIMEOUT_US);
    if (read != (int)sizeof(buffer))
    {
        set_error(sensor, "I2C read failed");
        return false;
    }

    *value = (uint16_t)((buffer[0] << 8) | buffer[1]);
    return true;
}

static bool probe_address(i2c_inst_t *i2c, uint8_t address)
{
    uint8_t reg = INA219_REG_CONFIG;
    int written = i2c_write_timeout_us(i2c,
                                       address,
                                       &reg,
                                       1,
                                       false,
                                       INA219_I2C_TIMEOUT_US);
    return written == 1;
}

static bool resolve_address(ina219_wattmeter_t *sensor, const ina219_wattmeter_config_t *config)
{
    if (config->address != 0u)
    {
        if (!probe_address(config->i2c, config->address))
        {
            set_error(sensor, "Configured INA219 address did not respond");
            return false;
        }

        sensor->address = config->address;
        return true;
    }

    for (size_t i = 0; i < sizeof(INA219_KNOWN_ADDRESSES); ++i)
    {
        if (probe_address(config->i2c, INA219_KNOWN_ADDRESSES[i]))
        {
            sensor->address = INA219_KNOWN_ADDRESSES[i];
            return true;
        }
    }

    set_error(sensor, "No INA219 found at 0x40/0x41/0x44/0x45");
    return false;
}

void ina219_wattmeter_init_default_config(ina219_wattmeter_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->i2c = i2c1;
    config->sda_pin = 6u;
    config->scl_pin = 7u;
    config->baudrate_hz = 100000u;
    config->address = 0u;
}

bool ina219_wattmeter_init(ina219_wattmeter_t *sensor, const ina219_wattmeter_config_t *config)
{
    if (sensor == NULL || config == NULL || config->i2c == NULL)
    {
        return false;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->i2c = config->i2c;
    sensor->last_error = "Not initialized";

    i2c_init(config->i2c, config->baudrate_hz);
    gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(config->sda_pin);
    gpio_pull_up(config->scl_pin);

    if (!resolve_address(sensor, config))
    {
        return false;
    }

    if (!write_register(sensor, INA219_REG_CONFIG, INA219_DEFAULT_CONFIG))
    {
        return false;
    }

    sensor->calibration_value = INA219_DEFAULT_CALIBRATION;
    if (!write_register(sensor, INA219_REG_CALIBRATION, sensor->calibration_value))
    {
        return false;
    }

    sensor->initialized = true;
    sensor->last_error = NULL;
    return true;
}

bool ina219_wattmeter_read(ina219_wattmeter_t *sensor, ina219_wattmeter_reading_t *reading)
{
    if (sensor == NULL || reading == NULL)
    {
        return false;
    }

    if (!sensor->initialized)
    {
        set_error(sensor, "INA219 is not initialized");
        return false;
    }

    uint16_t raw_bus_voltage = 0;
    uint16_t raw_shunt_voltage = 0;
    uint16_t raw_current = 0;
    uint16_t raw_power = 0;

    if (!read_register(sensor, INA219_REG_BUSVOLTAGE, &raw_bus_voltage) ||
        !read_register(sensor, INA219_REG_SHUNTVOLTAGE, &raw_shunt_voltage) ||
        !read_register(sensor, INA219_REG_CURRENT, &raw_current) ||
        !read_register(sensor, INA219_REG_POWER, &raw_power))
    {
        return false;
    }

    reading->bus_voltage_v = (float)(raw_bus_voltage >> 3) * 0.004f;
    reading->shunt_voltage_mv = (float)(int16_t)raw_shunt_voltage * 0.01f;
    reading->current_ma = (float)(int16_t)raw_current;
    reading->power_mw = (float)raw_power * 20.0f;
    reading->computed_power_mw = reading->bus_voltage_v * reading->current_ma;

    sensor->last_error = NULL;
    return true;
}

bool ina219_wattmeter_linear_calibrate(ina219_wattmeter_t *sensor,
                                       float ina219_reading_ma,
                                       float actual_reading_ma)
{
    if (sensor == NULL || !sensor->initialized)
    {
        return false;
    }

    if (ina219_reading_ma == 0.0f || actual_reading_ma == 0.0f)
    {
        set_error(sensor, "Calibration values must be non-zero");
        return false;
    }

    float ratio = actual_reading_ma / ina219_reading_ma;
    uint16_t new_calibration = (uint16_t)(ratio * (float)sensor->calibration_value) & 0xFFFEu;
    if (new_calibration == 0u)
    {
        set_error(sensor, "Calibration produced zero");
        return false;
    }

    sensor->calibration_value = new_calibration;
    if (!write_register(sensor, INA219_REG_CALIBRATION, sensor->calibration_value))
    {
        return false;
    }

    sensor->last_error = NULL;
    return true;
}

const char *ina219_wattmeter_last_error(const ina219_wattmeter_t *sensor)
{
    if (sensor == NULL || sensor->last_error == NULL)
    {
        return "";
    }
    return sensor->last_error;
}
