#include "controls/calibration.h"
#include <math.h>

static const AdcCalibration nominal[CAL_BANKS] = {
    {819.0f, 3276.0f},
    {2457.0f, 3685.5f},
};
static AdcCalibration current[CAL_BANKS] = {
    {819.0f, 3276.0f},
    {2457.0f, 3685.5f},
};
static uint32_t revisions[CAL_BANKS];
static unsigned count, sum, minimum, maximum;
static bool capturing, ready;
static CalibrationBank capture_bank;

void calibration_defaults(void)
{
    for (unsigned bank = 0; bank < CAL_BANKS; ++bank)
    {
        current[bank] = nominal[bank];
        ++revisions[bank];
    }
}

AdcCalibration calibration_nominal(CalibrationBank bank)
{
    return nominal[(unsigned)bank < CAL_BANKS ? bank : CAL_PICO];
}

AdcCalibration calibration_get(CalibrationBank bank)
{
    return current[(unsigned)bank < CAL_BANKS ? bank : CAL_PICO];
}

uint32_t calibration_revision(CalibrationBank bank)
{
    return (unsigned)bank < CAL_BANKS ? revisions[bank] : 0;
}

bool calibration_valid(CalibrationBank bank, AdcCalibration value)
{
    if ((unsigned)bank >= CAL_BANKS)
    {
        return false;
    }

    float span = value.raw_4v - value.raw_1v;
    float nominal_span = nominal[bank].raw_4v - nominal[bank].raw_1v;
    return isfinite(value.raw_1v) && isfinite(value.raw_4v) && value.raw_1v > 5 &&
           value.raw_4v < 4090 && span >= nominal_span * 0.4f && span <= nominal_span * 1.5f;
}

bool calibration_set(CalibrationBank bank, AdcCalibration value)
{
    if (!calibration_valid(bank, value))
    {
        return false;
    }

    current[bank] = value;
    ++revisions[bank];
    return true;
}

float calibration_adc_volts(CalibrationBank bank, uint16_t raw)
{
    if ((unsigned)bank >= CAL_BANKS)
    {
        return 0;
    }

    if (raw > 4095)
    {
        raw = 4095;
    }

    float low = bank == CAL_PICO ? 0.6f : 1.8f;
    float high = bank == CAL_PICO ? 2.4f : 2.7f;
    return low + (raw - current[bank].raw_1v) * (high - low) /
                     (current[bank].raw_4v - current[bank].raw_1v);
}

float calibration_normalized(CalibrationBank bank, uint16_t raw)
{
    float value = calibration_adc_volts(bank, raw) / 3.0f;
    return value < 0 ? 0 : value > 1 ? 1 : value;
}

float calibration_volts(uint16_t raw)
{
    return calibration_adc_volts(CAL_PICO, raw) / 0.6f;
}

void calibration_capture_begin(CalibrationBank bank)
{
    count = sum = maximum = 0;
    minimum = 4095;
    capture_bank = bank;
    capturing = (unsigned)bank < CAL_BANKS;
    ready = false;
}

void calibration_capture_cancel(void)
{
    capturing = ready = false;
}

void calibration_sample(CalibrationBank bank, uint16_t raw)
{
    if (!capturing || bank != capture_bank)
    {
        return;
    }

    if (raw > 4095)
    {
        raw = 4095;
    }

    sum += raw;
    if (raw < minimum)
    {
        minimum = raw;
    }

    if (raw > maximum)
    {
        maximum = raw;
    }

    if (++count == 64)
    {
        capturing = false;
        ready = true;
    }
}

bool calibration_capture_result(float *value, bool *success)
{
    if (!ready)
    {
        return false;
    }

    *value = sum / 64.0f;
    *success = maximum - minimum <= 16 && minimum > 5 && maximum < 4090;
    return true;
}
