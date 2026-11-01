#include "settings/settings.h"
#include "settings/settings_format.h"
#include "messaging/messenger.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern uint8_t test_flash[2][SETTINGS_PAGE_SIZE];
extern unsigned test_flash_writes;
extern bool test_interrupt_write;

bool add_message(MonkEventType type, float value)
{
    (void)type;
    (void)value;
    return true;
}

static void boot(void)
{
    routing_init();
    calibration_defaults();
    settings_load();
}

int main(void)
{
    memset(test_flash, 0xff, sizeof(test_flash));
    boot();
    assert(!settings_dirty());
    routing_set_target(0, TARGET_VOWEL);
    assert(calibration_set(CAL_PICO, (AdcCalibration){900, 3300}));
    assert(calibration_set(CAL_MCP3208, (AdcCalibration){2500, 3720}));
    assert(settings_dirty());
    settings_request_save();
    settings_poll(false); // display can still be using flash during startup
    assert(test_flash_writes == 0 && settings_status() == SETTINGS_PENDING);
    settings_poll(true);
    assert(settings_status() == SETTINGS_SAVED && !settings_dirty());
    boot();
    assert(routing_get_target(0) == TARGET_VOWEL && calibration_get(CAL_PICO).raw_1v == 900);
    assert(calibration_get(CAL_MCP3208).raw_1v == 2500);
    settings_request_save();
    settings_poll(true);
    assert(test_flash_writes == 1); // unchanged settings do not wear flash
    routing_set_target(7, TARGET_RELEASE);
    settings_request_save();
    settings_poll(true);
    assert(test_flash_writes == 2);
    boot();
    assert(routing_get_target(7) == TARGET_RELEASE);
    test_flash[1][20] ^= 1; // corrupt newest record, recover previous
    boot();
    assert(routing_get_target(0) == TARGET_VOWEL && routing_get_target(7) == TARGET_OFF);
    routing_set_target(2, TARGET_LEVEL);
    test_interrupt_write = true;
    settings_request_save();
    settings_poll(true);
    assert(settings_status() == SETTINGS_ERROR && settings_dirty());
    test_interrupt_write = false;
    boot();
    assert(routing_get_target(0) == TARGET_VOWEL && routing_get_target(2) == TARGET_OFF);
    settings_restore_defaults();
    assert(settings_dirty() && calibration_get(CAL_PICO).raw_1v == 819);
    settings_request_save();
    settings_poll(true);
    boot();
    for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
    {
        assert(routing_get_target(i) == TARGET_OFF);
    }

    assert(!settings_dirty());

    uint8_t page[SETTINGS_PAGE_SIZE];
    SavedSettings data = {.adc = {{819, 3276}, {2457, 3685.5f}}}, output;
    uint32_t sequence;
    settings_encode(page, &data, 42);
    assert(settings_decode(page, &output, &sequence) && sequence == 42);
    page[128] ^= 1;
    assert(!settings_decode(page, &output, &sequence));
    data.assignments[0] = 255;
    settings_encode(page, &data, 43);
    assert(!settings_decode(page, &output, &sequence));
    data.assignments[0] = 0;
    data.adc[CAL_PICO].raw_1v = NAN;
    settings_encode(page, &data, 44);
    assert(!settings_decode(page, &output, &sequence));

    assert(!calibration_set(CAL_PICO, (AdcCalibration){3300, 900}));
    assert(!calibration_set(CAL_PICO, (AdcCalibration){0, 4095}));
    calibration_capture_begin(CAL_PICO);
    float value;
    bool success;
    for (unsigned i = 0; i < 63; ++i)
    {
        calibration_sample(CAL_PICO, 819);
    }

    assert(!calibration_capture_result(&value, &success));
    calibration_sample(CAL_PICO, 819);
    assert(calibration_capture_result(&value, &success) && success && value == 819);
    calibration_capture_begin(CAL_PICO);
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, i & 1 ? 1000 : 2000);
    }

    assert(calibration_capture_result(&value, &success) && !success);
    calibration_defaults();
    assert(fabsf(calibration_adc_volts(CAL_PICO, 819) - 0.6f) < 0.0001f);
    assert(fabsf(calibration_adc_volts(CAL_MCP3208, 2457) - 1.8f) < 0.0001f);
    uint32_t pico_revision = calibration_revision(CAL_PICO);
    assert(calibration_set(CAL_MCP3208, (AdcCalibration){2500, 3720}));
    assert(calibration_revision(CAL_PICO) == pico_revision);
    assert(fabsf(calibration_normalized(CAL_MCP3208, 2500) - 0.6f) < 0.0001f);
    assert(fabsf(calibration_normalized(CAL_PICO, 819) - 0.2f) < 0.0001f);
    calibration_capture_begin(CAL_MCP3208);
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, 819);
    }

    assert(!calibration_capture_result(&value, &success));
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_MCP3208, 2500);
    }

    assert(calibration_capture_result(&value, &success) && success && value == 2500);

    // Legacy v1 records contained only the Pico calibration. Migrate with
    // nominal MCP values even if the unused legacy page bytes are erased.
    data = (SavedSettings){.adc = {{900, 3300}, {2457, 3685.5f}}};
    settings_encode(page, &data, 99);
    page[4] = 1;
    memset(page + 28, 0xff, 8);
    uint32_t crc = ~0u;
    for (unsigned i = 0; i < 252; ++i)
    {
        crc ^= page[i];
        for (unsigned b = 0; b < 8; ++b)
        {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }

    crc = ~crc;
    for (unsigned b = 0; b < 4; ++b)
    {
        page[252 + b] = crc >> (b * 8);
    }

    assert(settings_decode(page, &output, &sequence) && sequence == 99);
    assert(output.adc[CAL_PICO].raw_1v == 900);
    assert(output.adc[CAL_MCP3208].raw_1v == 2457);
    memcpy(test_flash[0], page, sizeof(page));
    memset(test_flash[1], 0xff, sizeof(test_flash[1]));
    boot();
    assert(calibration_get(CAL_PICO).raw_1v == 900 && calibration_get(CAL_MCP3208).raw_1v == 2457);
    puts("Both ADC banks, persistence, legacy migration, and interrupted save tests passed");
}
