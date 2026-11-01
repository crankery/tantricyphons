#ifndef TANTRICYPHONS_SETTINGS_FORMAT_H
#define TANTRICYPHONS_SETTINGS_FORMAT_H

#include "controls/calibration.h"
#include "controls/routing.h"
#define SETTINGS_PAGE_SIZE 256

typedef struct
{
    uint8_t assignments[ROUTING_INPUTS];
    AdcCalibration adc[CAL_BANKS];
} SavedSettings;

void settings_encode(uint8_t page[SETTINGS_PAGE_SIZE], const SavedSettings *settings,
                     uint32_t sequence);
bool settings_decode(const uint8_t page[SETTINGS_PAGE_SIZE], SavedSettings *settings,
                     uint32_t *sequence);

#endif
