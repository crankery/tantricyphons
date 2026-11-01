#include "settings/settings_format.h"
#include <string.h>
_Static_assert(sizeof(float) == 4, "Settings require 32-bit float");

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        p[i] = v >> (i * 8);
    }
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        value |= (uint32_t)p[i] << (i * 8);
    }

    return value;
}

static uint32_t crc32(const uint8_t *p, unsigned size)
{
    uint32_t crc = ~0u;
    for (unsigned i = 0; i < size; ++i)
    {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }

    return ~crc;
}

void settings_encode(uint8_t page[SETTINGS_PAGE_SIZE], const SavedSettings *settings,
                     uint32_t sequence)
{
    memset(page, 0xff, SETTINGS_PAGE_SIZE);
    // Keep the original four-byte signature so existing saved settings remain readable.
    memcpy(page, "LAMA", 4);
    put32(page + 4, 2); // version: target IDs must remain stable within this version
    put32(page + 8, sequence);
    memcpy(page + 12, settings->assignments, ROUTING_INPUTS);
    for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
    {
        uint32_t bits;
        unsigned offset = 20 + bank * 8;
        memcpy(&bits, &settings->adc[bank].raw_1v, 4);
        put32(page + offset, bits);
        memcpy(&bits, &settings->adc[bank].raw_4v, 4);
        put32(page + offset + 4, bits);
    }

    put32(page + 252, crc32(page, 252));
}

bool settings_decode(const uint8_t page[SETTINGS_PAGE_SIZE], SavedSettings *settings,
                     uint32_t *sequence)
{
    uint32_t version = get32(page + 4);
    if (memcmp(page, "LAMA", 4) || (version != 1 && version != 2) ||
        get32(page + 252) != crc32(page, 252))
    {
        return false;
    }

    SavedSettings decoded;
    memcpy(decoded.assignments, page + 12, ROUTING_INPUTS);
    for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
    {
        if (decoded.assignments[i] >= TARGET_COUNT)
        {
            return false;
        }
    }

    for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
    {
        if (version == 1 && bank == CAL_MCP3208)
        {
            decoded.adc[bank] = calibration_nominal(CAL_MCP3208);
        }
        else
        {
            uint32_t bits = get32(page + 20 + bank * 8);
            memcpy(&decoded.adc[bank].raw_1v, &bits, 4);
            bits = get32(page + 24 + bank * 8);
            memcpy(&decoded.adc[bank].raw_4v, &bits, 4);
        }

        if (!calibration_valid((CalibrationBank)bank, decoded.adc[bank]))
        {
            return false;
        }
    }

    *settings = decoded;
    *sequence = get32(page + 8);
    return true;
}
