#include "settings/settings_storage.h"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include <string.h>

#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)
_Static_assert(PICO_FLASH_SIZE_BYTES == 4194304, "Update settings reservation for this board");
_Static_assert(FLASH_PAGE_SIZE == SETTINGS_PAGE_SIZE, "Unexpected flash page size");
extern char __flash_binary_end;

static struct
{
    uint32_t offset;
    uint8_t page[FLASH_PAGE_SIZE];
} write_job;

const uint8_t *settings_flash_read(unsigned slot)
{
    assert(slot < 2);
    return (const uint8_t *)(XIP_BASE + SETTINGS_OFFSET + slot * FLASH_SECTOR_SIZE);
}

static void __not_in_flash_func(write_settings)(void *unused)
{
    (void)unused;
    flash_range_erase(write_job.offset, FLASH_SECTOR_SIZE);
    flash_range_program(write_job.offset, write_job.page, FLASH_PAGE_SIZE);
}

bool settings_flash_write(unsigned slot, const uint8_t page[SETTINGS_PAGE_SIZE])
{
    if (slot > 1 || (uintptr_t)&__flash_binary_end > XIP_BASE + SETTINGS_OFFSET)
    {
        return false;
    }

    write_job.offset = SETTINGS_OFFSET + slot * FLASH_SECTOR_SIZE;
    memcpy(write_job.page, page, FLASH_PAGE_SIZE);
    // Core 1 initializes the lockout victim before starting audio. All DMA
    // sources must be in RAM or idle; display readiness gates this call.
    return flash_safe_execute(write_settings, NULL, 100) == PICO_OK;
}
