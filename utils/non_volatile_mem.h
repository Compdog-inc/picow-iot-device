#ifndef _NVMEM_H
#define _NVMEM_H

#include <hardware/flash.h>
#include <hardware/sync.h>

#define FLASH_SECTOR_COUNT PICO_FLASH_SIZE_BYTES / FLASH_SECTOR_SIZE
#define FLASH_PAGE_COUNT PICO_FLASH_SIZE_BYTES / FLASH_PAGE_SIZE
#define FLASH_TARGET_OFFSET ((FLASH_SECTOR_COUNT - 1) * FLASH_SECTOR_SIZE)

static inline const uint8_t *nvmem_read(uint offset)
{
    return (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET + offset);
}

static inline void nvmem_reset()
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
}

static inline void nvmem_write(uint8_t *data, size_t count)
{
    uint8_t bytes[FLASH_PAGE_SIZE];
    for (size_t i = 0; i < count; i++)
    {
        bytes[i] = data[i];
    }
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(FLASH_TARGET_OFFSET, &bytes[0], FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

#endif