#ifndef TANTRICYPHONS_SETTINGS_STORAGE_H
#define TANTRICYPHONS_SETTINGS_STORAGE_H
#include "settings/settings_format.h"
const uint8_t *settings_flash_read(unsigned slot);
bool settings_flash_write(unsigned slot, const uint8_t page[SETTINGS_PAGE_SIZE]);
#endif
