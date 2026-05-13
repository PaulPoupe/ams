#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/address_mapped.h"
#include "device_config.h"

#define DEVICE_CONFIG_MAGIC 0x43464731u
#define DEVICE_CONFIG_VERSION 3u
#define DEVICE_CONFIG_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    device_config_t config;
} device_config_record_t;

typedef device_config_t device_config_v1_t;
typedef device_config_t device_config_v2_t;
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    device_config_v1_t config;
} device_config_record_v1_t;
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    device_config_v2_t config;
} device_config_record_v2_t;

static uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static const device_config_record_t *get_flash_record(void)
{
    return (const device_config_record_t *)(XIP_BASE + DEVICE_CONFIG_FLASH_OFFSET);
}

static void terminate_config_strings(device_config_t *config)
{
    config->ssid[DEVICE_CONFIG_SSID_MAX] = '\0';
    config->password[DEVICE_CONFIG_PASSWORD_MAX] = '\0';
    config->server_ip[DEVICE_CONFIG_SERVER_IP_MAX] = '\0';
    config->device_id[DEVICE_CONFIG_ID_MAX] = '\0';
}

static bool config_has_required_fields(const device_config_t *config)
{
    return config->ssid[0] != '\0' &&
           config->server_ip[0] != '\0' &&
           config->device_id[0] != '\0';
}

static bool load_config_payload(device_config_t *config,
                                const void *payload,
                                size_t payload_size,
                                uint32_t expected_crc)
{
    if (crc32_compute((const uint8_t *)payload, payload_size) != expected_crc)
    {
        return false;
    }

    memset(config, 0, sizeof(*config));

    size_t bytes_to_copy = payload_size;
    if (bytes_to_copy > sizeof(*config))
    {
        bytes_to_copy = sizeof(*config);
    }
    memcpy(config, payload, bytes_to_copy);

    terminate_config_strings(config);
    return config_has_required_fields(config);
}

static bool sanitize_config_for_storage(const device_config_t *config, device_config_t *sanitized_config)
{
    if (config == NULL || sanitized_config == NULL)
    {
        return false;
    }

    memset(sanitized_config, 0, sizeof(*sanitized_config));
    memcpy(sanitized_config, config, sizeof(*sanitized_config));
    terminate_config_strings(sanitized_config);
    return config_has_required_fields(sanitized_config);
}

bool device_config_load(device_config_t *config)
{
    if (config == NULL)
    {
        return false;
    }

    const device_config_record_t *record = get_flash_record();
    if (record->magic != DEVICE_CONFIG_MAGIC)
    {
        return false;
    }

    if (record->version == 1u)
    {
        const device_config_record_v1_t *record_v1 = (const device_config_record_v1_t *)record;
        return load_config_payload(config, &record_v1->config, sizeof(record_v1->config), record_v1->crc32);
    }

    if (record->version == 2u)
    {
        const device_config_record_v2_t *record_v2 = (const device_config_record_v2_t *)record;
        return load_config_payload(config, &record_v2->config, sizeof(record_v2->config), record_v2->crc32);
    }

    if (record->version != DEVICE_CONFIG_VERSION)
    {
        return false;
    }

    return load_config_payload(config, &record->config, sizeof(record->config), record->crc32);
}

bool device_config_save(const device_config_t *config)
{
    device_config_t sanitized_config;
    if (!sanitize_config_for_storage(config, &sanitized_config))
    {
        return false;
    }

    device_config_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = DEVICE_CONFIG_MAGIC;
    record.version = DEVICE_CONFIG_VERSION;
    memcpy(&record.config, &sanitized_config, sizeof(record.config));
    terminate_config_strings(&record.config);
    record.crc32 = crc32_compute((const uint8_t *)&record.config, sizeof(record.config));

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &record, sizeof(record));

    uint32_t irq_state = save_and_disable_interrupts();
    flash_range_erase(DEVICE_CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(DEVICE_CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq_state);

    return true;
}
