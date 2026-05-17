#ifndef SD_CARD_BUFFER_H
#define SD_CARD_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SD_CARD_AUDIO_BUFFER_SECONDS 10u
#define SD_CARD_AUDIO_SAMPLE_RATE_HZ 16000u
#define SD_CARD_AUDIO_BYTES_PER_SAMPLE 2u
#define SD_CARD_BLOCK_SIZE 512u
#define SD_CARD_AUDIO_BUFFER_BYTES (SD_CARD_AUDIO_BUFFER_SECONDS * SD_CARD_AUDIO_SAMPLE_RATE_HZ * SD_CARD_AUDIO_BYTES_PER_SAMPLE)
#define SD_CARD_AUDIO_BUFFER_BLOCKS (SD_CARD_AUDIO_BUFFER_BYTES / SD_CARD_BLOCK_SIZE)

typedef struct
{
    bool initialized;
    bool ready;
    bool block_addressed;
    uint32_t base_block;
    uint32_t block_count;
    uint32_t next_block_index;
    uint32_t written_blocks;
    uint32_t failed_writes;
    uint16_t pending_bytes;
    uint8_t pending_block[SD_CARD_BLOCK_SIZE];
    const char *last_error;
} sd_card_buffer_t;

bool sd_card_buffer_init(sd_card_buffer_t *buffer);
bool sd_card_buffer_is_initialized(const sd_card_buffer_t *buffer);
bool sd_card_buffer_is_ready(const sd_card_buffer_t *buffer);
bool sd_card_buffer_write_audio(sd_card_buffer_t *buffer, const int16_t *samples, size_t sample_count);
const char *sd_card_buffer_last_error(const sd_card_buffer_t *buffer);

#endif
