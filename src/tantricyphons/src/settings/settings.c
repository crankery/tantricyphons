#include "settings/settings.h"
#include "settings/settings_format.h"
#include "settings/settings_storage.h"
#include <string.h>

static int active_slot = -1;
static uint32_t sequence;
static SavedSettings saved;
static SettingsStatus status;

static SavedSettings snapshot(void)
{
    SavedSettings value = {0};
    for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
    {
        value.adc[bank] = calibration_get((CalibrationBank)bank);
    }

    for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
    {
        value.assignments[i] = routing_get_target(i);
    }

    return value;
}

static bool same(SavedSettings a, SavedSettings b)
{
    if (memcmp(a.assignments, b.assignments, sizeof(a.assignments)))
    {
        return false;
    }

    for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
    {
        if (a.adc[bank].raw_1v != b.adc[bank].raw_1v || a.adc[bank].raw_4v != b.adc[bank].raw_4v)
        {
            return false;
        }
    }

    return true;
}

void settings_load(void)
{
    active_slot = -1;
    sequence = 0;
    status = SETTINGS_IDLE;
    SavedSettings value;
    uint32_t candidate;
    for (int slot = 0; slot < 2; ++slot)
    {
        const uint8_t *page = settings_flash_read((unsigned)slot);
        if (settings_decode(page, &value, &candidate) &&
            (active_slot < 0 || (int32_t)(candidate - sequence) > 0))
        {
            active_slot = slot;
            sequence = candidate;
            saved = value;
        }
    }

    if (active_slot >= 0)
    {
        for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
        {
            calibration_set((CalibrationBank)bank, saved.adc[bank]);
        }

        for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
        {
            routing_set_target(i, (RoutingTarget)saved.assignments[i]);
        }
    }
    else
    {
        saved = snapshot();
    }
}

bool settings_dirty(void)
{
    return !same(saved, snapshot());
}

SettingsStatus settings_status(void)
{
    return status;
}

void settings_request_save(void)
{
    status = SETTINGS_PENDING;
}

void settings_restore_defaults(void)
{
    routing_init();
    calibration_defaults();
    status = SETTINGS_IDLE;
}

void settings_poll(bool display_ready)
{
    if (status != SETTINGS_PENDING || !display_ready)
    {
        return;
    }

    SavedSettings current = snapshot();
    if (active_slot >= 0 && same(current, saved))
    {
        status = SETTINGS_SAVED;
        return;
    }

    int next = active_slot == 0 ? 1 : 0;
    uint8_t page_data[SETTINGS_PAGE_SIZE];
    settings_encode(page_data, &current, sequence + 1);
    bool written = settings_flash_write((unsigned)next, page_data);
    SavedSettings verify;
    uint32_t verify_sequence;
    const uint8_t *page = settings_flash_read((unsigned)next);
    if (!written || !settings_decode(page, &verify, &verify_sequence) ||
        verify_sequence != sequence + 1 || !same(current, verify))
    {
        status = SETTINGS_ERROR;
        return;
    }

    active_slot = next;
    sequence = verify_sequence;
    saved = current;
    status = SETTINGS_SAVED;
}
