#include "audio/voice_control.h"
#include "controls/cv_input.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static VoiceControl control;
static MonkVoice voice;
static float level = 1;

static void event(MonkEventType type, float value, MonkEventSource source, uint64_t now)
{
    voice_control_event(
        &control, &voice, &level, (MonkEvent){.type = type, .value = value, .source = source}, now);
}

static void near(float a, float b)
{
    assert(fabsf(a - b) < 0.02f);
}

int main(void)
{
    monk_voice_init(&voice, 44100);
    voice_control_init(&control);
    near(cv_pitch_hz(0), 65.4064f);
    near(cv_pitch_hz(819), 130.8128f); // 0.6 V ADC = 1 V at jack
    near(cv_pitch_hz(4095), 2093.0045f);

    event(MONK_SET_PITCH, 220, MONK_SOURCE_ADC, 0);
    event(MONK_GATE_ON, 220, MONK_SOURCE_ADC, 0);
    assert(voice.active);
    near(voice.target_pitch, 57);
    event(MONK_SET_ATTACK, 1, MONK_SOURCE_ADC, 1);
    voice.env_stage = ENV_SUSTAIN;
    event(MONK_GATE_ON, 220, MONK_SOURCE_ADC, 10000);
    assert(voice.env_stage == ENV_SUSTAIN); // heartbeat must not retrigger
    event(MONK_SET_ATTACK, 0, MONK_SOURCE_ADC, 10001);
    event(MONK_NOTE_ON, 69 * 128 + 100, MONK_SOURCE_MIDI, 20000);
    near(voice.target_pitch, 69);
    event(MONK_SET_PITCH, 330, MONK_SOURCE_ADC, 21000);
    event(MONK_GATE_OFF, 0, MONK_SOURCE_ADC, 21000);
    near(voice.target_pitch, 69);
    assert(voice.active);
    event(MONK_NOTE_ON, 72 * 128 + 127, MONK_SOURCE_MIDI, 22000);
    event(MONK_NOTE_OFF, 69, MONK_SOURCE_MIDI, 23000);
    near(voice.target_pitch, 72);
    event(MONK_NOTE_OFF, 72, MONK_SOURCE_MIDI, 24000);
    assert(!voice.active && !control.held_count);

    // CV gate goes high under MIDI; releasing the final key must restore
    // the latest CV pitch without requiring a new gate edge or ADC change.
    event(MONK_NOTE_ON, 60 * 128 + 100, MONK_SOURCE_MIDI, 30000);
    event(MONK_GATE_ON, 330, MONK_SOURCE_ADC, 31000);
    event(MONK_SET_PITCH, 440, MONK_SOURCE_ADC, 32000);
    near(voice.target_pitch, 60);
    event(MONK_NOTE_OFF, 60, MONK_SOURCE_MIDI, 33000);
    assert(voice.active);
    near(voice.target_pitch, 69);
    event(MONK_GATE_OFF, 0, MONK_SOURCE_ADC, 34000);
    assert(!voice.active);

    event(MONK_SET_VOWEL, 0.2f, MONK_SOURCE_ADC, 100000);
    event(MONK_SET_VOWEL, 0.9f, MONK_SOURCE_MIDI, 100000);
    event(MONK_SET_VOWEL, 0.3f, MONK_SOURCE_ADC, 200000);
    near(voice.target_vowel, 0.9f);
    event(MONK_SET_ASPIRATION, 0.4f, MONK_SOURCE_ADC, 200000);
    near(voice.aspiration_amp, 0.4f); // other panel parameters stay available
    voice_control_tick(&control, &voice, &level, 2099999);
    near(voice.target_vowel, 0.9f);
    voice_control_tick(&control, &voice, &level, 2100000);
    near(voice.target_vowel, 0.9f);
    voice_control_tick(&control, &voice, &level, 2150000);
    near(voice.target_vowel, 0.6f);
    voice_control_tick(&control, &voice, &level, 2200000);
    near(voice.target_vowel, 0.3f);
    event(MONK_SET_VOWEL, 0.5f, MONK_SOURCE_ADC, 2201000);
    near(voice.target_vowel, 0.5f);

    // A new MIDI controller message restarts its lease, including mid-blend.
    event(MONK_SET_LEVEL, 0.2f, MONK_SOURCE_ADC, 3000000);
    event(MONK_SET_LEVEL, 1, MONK_SOURCE_MIDI, 3000000);
    voice_control_tick(&control, &voice, &level, 5000000);
    voice_control_tick(&control, &voice, &level, 5050000);
    near(level, 0.6f);
    event(MONK_SET_LEVEL, 0.8f, MONK_SOURCE_MIDI, 5050000);
    voice_control_tick(&control, &voice, &level, 6000000);
    near(level, 0.8f);

    // Panic releases notes; periodic CV status can recover after overflow.
    event(MONK_GATE_ON, 220, MONK_SOURCE_ADC, 6000000);
    event(MONK_NOTE_ON, 69 * 128 + 127, MONK_SOURCE_MIDI, 6000000);
    voice_control_panic(&control, &voice);
    assert(!voice.active && !control.cv_gate && !control.held_count);
    event(MONK_GATE_ON, 220, MONK_SOURCE_ADC, 6010000);
    assert(voice.active);
    near(voice.target_pitch, 57);
    event(MONK_NOTE_ON, 69 * 128 + 127, MONK_SOURCE_MIDI, 6020000);
    event(MONK_SET_PITCH, 330, MONK_SOURCE_ADC, 6021000);
    event(MONK_ALL_NOTES_OFF, 0, MONK_SOURCE_MIDI, 6022000);
    assert(voice.active && !control.held_count);
    near(voice.target_pitch, monk_hz_to_note(330));
    puts("CV/MIDI ownership, handback, gate, and pitch calibration tests passed");
}
