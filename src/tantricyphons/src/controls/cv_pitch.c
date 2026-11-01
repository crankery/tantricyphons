#include "controls/cv_input.h"
#include "audio/voice.h"
#include "controls/calibration.h"

float cv_pitch_hz(uint16_t raw)
{
    if (raw > 4095)
    {
        raw = 4095;
    }

    float volts = calibration_volts(raw);
    return monk_note_to_hz(CV_ZERO_NOTE + volts * 12.0f);
}
