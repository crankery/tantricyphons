#ifndef _MESSENGER_DOT_H
#define _MESSENGER_DOT_H

#include "stdbool.h"
#include "audio/voice.h"

typedef enum
{
    MONK_SET_PITCH,
    MONK_SET_VOWEL,
    MONK_SET_VIBRATO,
    MONK_SET_ASPIRATION,
    MONK_SET_LEVEL,
    MONK_SET_VIBRATO_RATE,
    MONK_SET_ATTACK,
    MONK_SET_DECAY,
    MONK_SET_SUSTAIN,
    MONK_SET_RELEASE,
    MONK_GATE_ON,
    MONK_SET_GLIDE,
    MONK_SET_VOICE,
    MONK_NOTE_ON, /* value = note * 128 + velocity */
    MONK_NOTE_OFF,
    MONK_ALL_NOTES_OFF,
    MONK_GATE_OFF
} MonkEventType;

typedef enum
{
    MONK_SOURCE_ADC,
    MONK_SOURCE_MIDI
} MonkEventSource;

typedef struct
{
    MonkEventType type;
    float value;
    MonkEventSource source;
} MonkEvent;

void start_messenger();
bool add_message(MonkEventType e, float v);
bool add_midi_message(MonkEventType e, float v);
void process_messages(MonkVoice *voice, float *master_level);

#endif // _MESSENGER_DOT_H