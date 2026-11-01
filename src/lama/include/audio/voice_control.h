#ifndef LAMA_VOICE_CONTROL_H
#define LAMA_VOICE_CONTROL_H
#include "messaging/messenger.h"

#define MIDI_CONTROL_TIMEOUT_US 2000000u
#define CONTROL_RETURN_US 100000u

typedef struct
{
    float adc_value, midi_value;
    bool adc_valid, owned, returning;
    uint64_t until, return_at;
} ParameterControl;

typedef struct
{
    uint8_t held[128];
    unsigned held_count;
    bool cv_gate;
    float cv_pitch;
    ParameterControl parameters[MONK_GATE_OFF + 1];
} VoiceControl;

void voice_control_init(VoiceControl *control);
/* Audio-core only. now_us is supplied explicitly for deterministic tests. */
void voice_control_event(VoiceControl *control, MonkVoice *voice, float *level, MonkEvent event,
                         uint64_t now_us);
void voice_control_tick(VoiceControl *control, MonkVoice *voice, float *level, uint64_t now_us);
/* Queue failure: stop all notes, including CV; fresh CV status can resume. */
void voice_control_panic(VoiceControl *control, MonkVoice *voice);
#endif
