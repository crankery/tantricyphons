/* Host flash model: interrupted programs leave the alternate record invalid. */
#include "settings/settings_storage.h"
#include <assert.h>
#include <string.h>
uint8_t test_flash[2][SETTINGS_PAGE_SIZE];
unsigned test_flash_writes;
bool test_interrupt_write;

const uint8_t *settings_flash_read(unsigned slot)
{
    assert(slot < 2);
    return test_flash[slot];
}

bool settings_flash_write(unsigned slot, const uint8_t page[SETTINGS_PAGE_SIZE])
{
    assert(slot < 2);
    ++test_flash_writes;
    memset(test_flash[slot], 0xff, SETTINGS_PAGE_SIZE);
    memcpy(test_flash[slot], page, test_interrupt_write ? 64 : SETTINGS_PAGE_SIZE);
    return !test_interrupt_write;
}
