#include "sd_card_buffer.h"

#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define SD_SPI spi1
#define SD_PIN_MISO 8u
#define SD_PIN_CS 9u
#define SD_PIN_SCK 10u
#define SD_PIN_MOSI 11u

#define SD_SPI_INIT_BAUD_HZ 400000u
#define SD_SPI_RUN_BAUD_HZ 8000000u
#define SD_IDLE_TIMEOUT_MS 1500u
#define SD_READY_TIMEOUT_MS 2000u
#define SD_DATA_TIMEOUT_MS 500u
#define SD_BUFFER_END_MARGIN_BLOCKS 1024u
#define SD_FALLBACK_BASE_BLOCK 2048u

#define SD_CMD0 0u
#define SD_CMD8 8u
#define SD_CMD9 9u
#define SD_CMD16 16u
#define SD_CMD24 24u
#define SD_CMD55 55u
#define SD_CMD58 58u
#define SD_ACMD41 41u

#define SD_R1_IDLE 0x01u
#define SD_R1_READY 0x00u
#define SD_DATA_TOKEN 0xFEu
#define SD_WRITE_ACCEPTED 0x05u

static void set_error(sd_card_buffer_t *buffer, const char *message)
{
    if (buffer != NULL)
    {
        buffer->last_error = message;
    }
}

static uint8_t spi_transfer(uint8_t value)
{
    uint8_t response = 0xFFu;
    spi_write_read_blocking(SD_SPI, &value, &response, 1);
    return response;
}

static void sd_select(void)
{
    gpio_put(SD_PIN_CS, 0);
    spi_transfer(0xFFu);
}

static void sd_deselect(void)
{
    gpio_put(SD_PIN_CS, 1);
    spi_transfer(0xFFu);
}

static uint8_t sd_send_command(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t packet[6] = {
        (uint8_t)(0x40u | cmd),
        (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8),
        (uint8_t)arg,
        crc};

    sd_select();
    spi_write_blocking(SD_SPI, packet, sizeof(packet));

    for (uint8_t i = 0; i < 10u; ++i)
    {
        uint8_t response = spi_transfer(0xFFu);
        if ((response & 0x80u) == 0u)
        {
            return response;
        }
    }

    return 0xFFu;
}

static bool sd_wait_ready(uint32_t timeout_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline))
    {
        if (spi_transfer(0xFFu) == 0xFFu)
        {
            return true;
        }
    }

    return false;
}

static bool sd_wait_token(uint8_t token, uint32_t timeout_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline))
    {
        if (spi_transfer(0xFFu) == token)
        {
            return true;
        }
    }

    return false;
}

static bool sd_read_data_block(uint8_t *data, size_t length)
{
    if (!sd_wait_token(SD_DATA_TOKEN, SD_DATA_TIMEOUT_MS))
    {
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        data[i] = spi_transfer(0xFFu);
    }

    spi_transfer(0xFFu);
    spi_transfer(0xFFu);
    return true;
}

static bool sd_read_ocr(uint8_t ocr[4])
{
    uint8_t response = sd_send_command(SD_CMD58, 0u, 0x01u);
    if (response != SD_R1_READY && response != SD_R1_IDLE)
    {
        sd_deselect();
        return false;
    }

    for (size_t i = 0; i < 4u; ++i)
    {
        ocr[i] = spi_transfer(0xFFu);
    }

    sd_deselect();
    return true;
}

static bool sd_read_csd(uint8_t csd[16])
{
    uint8_t response = sd_send_command(SD_CMD9, 0u, 0x01u);
    if (response != SD_R1_READY)
    {
        sd_deselect();
        return false;
    }

    bool ok = sd_read_data_block(csd, 16u);
    sd_deselect();
    return ok;
}

static uint32_t sd_parse_capacity_blocks(const uint8_t csd[16])
{
    uint8_t csd_version = (uint8_t)((csd[0] >> 6) & 0x03u);
    if (csd_version == 1u)
    {
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) |
                          ((uint32_t)csd[8] << 8) |
                          (uint32_t)csd[9];
        return (c_size + 1u) * 1024u;
    }

    if (csd_version == 0u)
    {
        uint32_t read_bl_len = csd[5] & 0x0Fu;
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10) |
                          ((uint32_t)csd[7] << 2) |
                          ((uint32_t)(csd[8] & 0xC0u) >> 6);
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03u) << 1) |
                               ((uint32_t)(csd[10] & 0x80u) >> 7);
        uint32_t block_len = 1u << read_bl_len;
        uint32_t block_count = (c_size + 1u) * (1u << (c_size_mult + 2u));
        uint64_t capacity_bytes = (uint64_t)block_count * (uint64_t)block_len;
        return (uint32_t)(capacity_bytes / SD_CARD_BLOCK_SIZE);
    }

    return 0u;
}

static bool sd_send_acmd41(bool v2_card)
{
    uint8_t response = sd_send_command(SD_CMD55, 0u, 0x01u);
    sd_deselect();
    if (response > SD_R1_IDLE)
    {
        return false;
    }

    response = sd_send_command(SD_ACMD41, v2_card ? 0x40000000u : 0u, 0x01u);
    sd_deselect();
    return response == SD_R1_READY;
}

static bool sd_initialize_card(sd_card_buffer_t *buffer)
{
    sd_deselect();
    for (uint8_t i = 0; i < 10u; ++i)
    {
        spi_transfer(0xFFu);
    }

    uint8_t response = 0xFFu;
    absolute_time_t idle_deadline = make_timeout_time_ms(SD_IDLE_TIMEOUT_MS);
    while (!time_reached(idle_deadline))
    {
        response = sd_send_command(SD_CMD0, 0u, 0x95u);
        sd_deselect();
        if (response == SD_R1_IDLE)
        {
            break;
        }
    }

    if (response != SD_R1_IDLE)
    {
        set_error(buffer, "SD CMD0 failed");
        return false;
    }

    bool v2_card = false;
    response = sd_send_command(SD_CMD8, 0x000001AAu, 0x87u);
    if (response == SD_R1_IDLE)
    {
        uint8_t r7[4] = {0};
        for (size_t i = 0; i < 4u; ++i)
        {
            r7[i] = spi_transfer(0xFFu);
        }
        v2_card = r7[2] == 0x01u && r7[3] == 0xAAu;
    }
    sd_deselect();

    absolute_time_t ready_deadline = make_timeout_time_ms(SD_READY_TIMEOUT_MS);
    bool ready = false;
    while (!time_reached(ready_deadline))
    {
        if (sd_send_acmd41(v2_card))
        {
            ready = true;
            break;
        }
        sleep_ms(10);
    }

    if (!ready)
    {
        set_error(buffer, "SD ACMD41 failed");
        return false;
    }

    uint8_t ocr[4] = {0};
    if (!sd_read_ocr(ocr))
    {
        set_error(buffer, "SD OCR read failed");
        return false;
    }
    buffer->block_addressed = v2_card && ((ocr[0] & 0x40u) != 0u);

    if (!buffer->block_addressed)
    {
        response = sd_send_command(SD_CMD16, SD_CARD_BLOCK_SIZE, 0x01u);
        sd_deselect();
        if (response != SD_R1_READY)
        {
            set_error(buffer, "SD CMD16 failed");
            return false;
        }
    }

    uint8_t csd[16] = {0};
    if (sd_read_csd(csd))
    {
        buffer->capacity_blocks = sd_parse_capacity_blocks(csd);
    }

    return true;
}

static bool sd_write_block(sd_card_buffer_t *buffer, uint32_t relative_block, const uint8_t data[SD_CARD_BLOCK_SIZE])
{
    uint32_t block = buffer->base_block + relative_block;
    uint32_t address = buffer->block_addressed ? block : block * SD_CARD_BLOCK_SIZE;
    uint8_t response = sd_send_command(SD_CMD24, address, 0x01u);
    if (response != SD_R1_READY)
    {
        sd_deselect();
        set_error(buffer, "SD CMD24 failed");
        return false;
    }

    spi_transfer(0xFFu);
    spi_transfer(SD_DATA_TOKEN);
    spi_write_blocking(SD_SPI, data, SD_CARD_BLOCK_SIZE);
    spi_transfer(0xFFu);
    spi_transfer(0xFFu);

    uint8_t data_response = (uint8_t)(spi_transfer(0xFFu) & 0x1Fu);
    if (data_response != SD_WRITE_ACCEPTED)
    {
        sd_deselect();
        set_error(buffer, "SD write rejected");
        return false;
    }

    if (!sd_wait_ready(SD_DATA_TIMEOUT_MS))
    {
        sd_deselect();
        set_error(buffer, "SD write timeout");
        return false;
    }

    sd_deselect();
    return true;
}

static bool sd_flush_pending_block(sd_card_buffer_t *buffer)
{
    if (buffer == NULL || !buffer->ready || buffer->pending_bytes != SD_CARD_BLOCK_SIZE)
    {
        return false;
    }

    if (!sd_write_block(buffer, buffer->next_block_index, buffer->pending_block))
    {
        buffer->ready = false;
        buffer->failed_writes++;
        printf("SD circular audio buffer write failed: %s\n", sd_card_buffer_last_error(buffer));
        return false;
    }

    buffer->next_block_index = (buffer->next_block_index + 1u) % buffer->block_count;
    buffer->pending_bytes = 0u;
    buffer->written_blocks++;
    return true;
}

bool sd_card_buffer_init(sd_card_buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return false;
    }

    memset(buffer, 0, sizeof(*buffer));
    buffer->initialized = true;
    buffer->last_error = "Not initialized";
    buffer->block_count = SD_CARD_AUDIO_BUFFER_BLOCKS;

    spi_init(SD_SPI, SD_SPI_INIT_BAUD_HZ);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(SD_PIN_CS);
    gpio_set_dir(SD_PIN_CS, GPIO_OUT);
    gpio_put(SD_PIN_CS, 1);
    gpio_pull_up(SD_PIN_MISO);
    gpio_pull_up(SD_PIN_CS);
    sleep_ms(10);

    if (!sd_initialize_card(buffer))
    {
        printf("SD card buffer unavailable: %s\n", sd_card_buffer_last_error(buffer));
        return false;
    }

    if (buffer->capacity_blocks > (SD_CARD_AUDIO_BUFFER_BLOCKS + SD_BUFFER_END_MARGIN_BLOCKS))
    {
        buffer->base_block = buffer->capacity_blocks - SD_BUFFER_END_MARGIN_BLOCKS - SD_CARD_AUDIO_BUFFER_BLOCKS;
    }
    else
    {
        buffer->base_block = SD_FALLBACK_BASE_BLOCK;
    }

    spi_set_baudrate(SD_SPI, SD_SPI_RUN_BAUD_HZ);
    memset(buffer->pending_block, 0, sizeof(buffer->pending_block));
    buffer->ready = true;
    buffer->last_error = NULL;

    printf("SD circular audio buffer ready: %u sec, %u bytes, base_block=%lu, blocks=%lu\n",
           (unsigned)SD_CARD_AUDIO_BUFFER_SECONDS,
           (unsigned)SD_CARD_AUDIO_BUFFER_BYTES,
           (unsigned long)buffer->base_block,
           (unsigned long)buffer->block_count);
    return true;
}

bool sd_card_buffer_is_initialized(const sd_card_buffer_t *buffer)
{
    return buffer != NULL && buffer->initialized;
}

bool sd_card_buffer_is_ready(const sd_card_buffer_t *buffer)
{
    return buffer != NULL && buffer->ready;
}

bool sd_card_buffer_write_audio(sd_card_buffer_t *buffer, const int16_t *samples, size_t sample_count)
{
    if (buffer == NULL || samples == NULL || sample_count == 0u)
    {
        return false;
    }

    if (!buffer->ready)
    {
        return false;
    }

    const uint8_t *source = (const uint8_t *)samples;
    size_t remaining = sample_count * sizeof(int16_t);
    while (remaining > 0u)
    {
        size_t free_bytes = SD_CARD_BLOCK_SIZE - buffer->pending_bytes;
        size_t copy_bytes = remaining < free_bytes ? remaining : free_bytes;
        memcpy(&buffer->pending_block[buffer->pending_bytes], source, copy_bytes);
        buffer->pending_bytes = (uint16_t)(buffer->pending_bytes + copy_bytes);
        source += copy_bytes;
        remaining -= copy_bytes;

        if (buffer->pending_bytes == SD_CARD_BLOCK_SIZE && !sd_flush_pending_block(buffer))
        {
            return false;
        }
    }

    return true;
}

const char *sd_card_buffer_last_error(const sd_card_buffer_t *buffer)
{
    if (buffer == NULL || buffer->last_error == NULL)
    {
        return "";
    }

    return buffer->last_error;
}
