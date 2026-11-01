#include "controls/vowel_controls.h"
#include "controls/calibration.h"
#include "messaging/messenger.h"
#include <math.h>
#include <string.h>

float vowel_controls_value(uint16_t pot, uint16_t cv)
{
    float pot_offset = calibration_normalized(CAL_PICO, pot) * 2.0f - 1.0f;
    float cv_volts = calibration_adc_volts(CAL_PICO, cv) / VOWEL_CV_GAIN;
    if (cv_volts < 0.0f)
    {
        cv_volts = 0.0f;
    }

    float value = 0.5f + pot_offset * 0.5f + cv_volts / VOWEL_CV_FULL_SCALE_VOLTS * VOWEL_CV_DEPTH;
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

void vowel_controls_init(VowelControls *controls)
{
    memset(controls, 0, sizeof(*controls));
}

void vowel_controls_sample(VowelControls *controls, unsigned channel, uint16_t raw)
{
    if (channel == 1)
    {
        controls->cv = raw;
        controls->seen |= 1;
    }
    else if (channel == 2)
    {
        controls->pot = raw;
        controls->seen |= 2;
    }
}

void vowel_controls_poll(VowelControls *controls, uint64_t now_us)
{
    if (controls->seen != 3)
    {
        return;
    }

    uint32_t revision = calibration_revision(CAL_PICO);
    float value = vowel_controls_value(controls->pot, controls->cv);
    if (controls->sent && revision == controls->calibration_revision &&
        (now_us - controls->sent_at < VOWEL_UPDATE_INTERVAL_US ||
         fabsf(value - controls->last_value) < VOWEL_CHANGE_THRESHOLD))
    {
        return;
    }

    if (add_message(MONK_SET_VOWEL, value))
    {
        controls->sent = true;
        controls->last_value = value;
        controls->sent_at = now_us;
        controls->calibration_revision = revision;
    }
}
