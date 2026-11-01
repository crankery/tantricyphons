#include "controls/vowel_controls.h"
#include "controls/calibration.h"
#include "audio/voice_control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static MonkEvent latest;
static unsigned messages;
static bool accept = true;

bool add_message(MonkEventType type, float value)
{
    if (!accept)
    {
        return false;
    }

    latest = (MonkEvent){.type = type, .value = value, .source = MONK_SOURCE_ADC};
    ++messages;
    return true;
}

static void near(float a, float b)
{
    assert(fabsf(a - b) < 0.001f);
}

int main(void)
{
    calibration_defaults();
    near(vowel_controls_value(0, 0), 0);
    near(vowel_controls_value(2048, 0), 0.5f);
    near(vowel_controls_value(4095, 0), 1);
    near(vowel_controls_value(0, 4095), 0.5f);
    near(vowel_controls_value(2048, 819), 0.6f); // CV at 1 V jack
    near(vowel_controls_value(2048, 4095), 1);
    near(vowel_controls_value(4095, 4095), 1);

    VowelControls controls;
    vowel_controls_init(&controls);
    vowel_controls_poll(&controls, 0);
    assert(messages == 0);
    vowel_controls_sample(&controls, 0, 4095); // pitch must not affect vowel
    vowel_controls_sample(&controls, 2, 2048);
    vowel_controls_poll(&controls, 0);
    assert(messages == 0); // need both readings, including actual zero CV
    vowel_controls_sample(&controls, 1, 0);
    vowel_controls_poll(&controls, 0);
    assert(messages == 1 && latest.type == MONK_SET_VOWEL);
    near(latest.value, 0.5f);
    vowel_controls_sample(&controls, 2, 2049);
    vowel_controls_poll(&controls, 5000);
    assert(messages == 1); // suppress ADC jitter
    vowel_controls_sample(&controls, 2, 3000);
    vowel_controls_poll(&controls, 1000);
    assert(messages == 1); // rate limit
    accept = false;
    vowel_controls_poll(&controls, 5000);
    assert(messages == 1);
    accept = true;
    vowel_controls_poll(&controls, 5001);
    assert(messages == 2); // retry a failed queue operation
    assert(calibration_set(CAL_PICO, (AdcCalibration){900, 3300}));
    vowel_controls_poll(&controls, 5002);
    assert(messages == 3); // same raw values refresh after calibration
    calibration_defaults();

    // Combined dedicated controls pass through the existing MIDI lease:
    // MIDI owns vowel, ADC changes are cached, then smooth handback restores
    // the combined pot/CV value rather than an individual input.
    static MonkVoice voice;
    VoiceControl ownership;
    float level = 1;
    monk_voice_init(&voice, 44100);
    voice_control_init(&ownership);
    voice_control_event(
        &ownership,
        &voice,
        &level,
        (MonkEvent){.type = MONK_SET_VOWEL, .value = 0.9f, .source = MONK_SOURCE_MIDI},
        10000);
    vowel_controls_sample(&controls, 2, 2048);
    vowel_controls_sample(&controls, 1, 0);
    vowel_controls_poll(&controls, 10001);
    voice_control_event(&ownership, &voice, &level, latest, 10001);
    near(voice.target_vowel, 0.9f);
    voice_control_tick(&ownership, &voice, &level, 2010000);
    voice_control_tick(&ownership, &voice, &level, 2110000);
    near(voice.target_vowel, 0.5f);
    puts("Dedicated vowel pot/CV, calibration, noise limits, and MIDI priority tests passed");
}
