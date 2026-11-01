#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "midi/midi.h"
#include "messaging/messenger.h"

static MonkEvent events[32];
static unsigned count;

bool add_message(MonkEventType type, float value)
{
    assert(count < 32);
    events[count++] = (MonkEvent){.type = type, .value = value};
    return true;
}

bool add_midi_message(MonkEventType type, float value)
{
    bool ok = add_message(type, value);
    events[count - 1].source = MONK_SOURCE_MIDI;
    return ok;
}

static void send(const uint8_t *bytes, unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
    {
        midi_receive_byte(bytes[i]);
    }
}

#define SEND(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        const uint8_t b[] = {__VA_ARGS__};                                                         \
        send(b, sizeof b);                                                                         \
    } while (0)

int main(void)
{
    midi_reset_parser();
    SEND(0x90, 60, 0xf8, 100, 64, 110, 60, 0);
    assert(count == 3);
    assert(events[0].type == MONK_NOTE_ON && events[0].value == 60 * 128 + 100);
    assert(events[1].type == MONK_NOTE_ON && events[1].value == 64 * 128 + 110);
    assert(events[2].type == MONK_NOTE_OFF && events[2].value == 60);
    SEND(0x91, 60, 100, 0xa0, 60, 100, 0xc0, 12, 0xd0, 90);
    assert(count == 3);
    SEND(0x90, 60, 0xf0, 1, 2, 0xf8, 0xf7, 100, 60, 100);
    assert(count == 3); /* SysEx cancels an incomplete message/running status. */
    SEND(0xb0, 1, 127, 5, 64, 7, 0, 13, 127, 12, 127);
    assert(count == 7); /* Delay is unsupported on the Pico. */
    assert(events[3].type == MONK_SET_VIBRATO && events[3].value == 1);
    assert(events[4].type == MONK_SET_GLIDE);
    assert(events[5].type == MONK_SET_LEVEL && events[5].value == 0);
    assert(events[6].type == MONK_SET_VOICE);
    SEND(0xe0, 0, 64, 0xb0, 123, 0, 120, 0, 0xff);
    assert(count == 11);
    assert(events[7].type == MONK_SET_VOWEL && events[7].value == 0.5f);
    for (unsigned i = 8; i < count; ++i)
    {
        assert(events[i].type == MONK_ALL_NOTES_OFF);
    }

    SEND(60, 100);
    assert(count == 11); /* Reset clears running status. */
    for (unsigned i = 0; i < count; ++i)
    {
        assert(events[i].source == MONK_SOURCE_MIDI);
    }

    puts("MIDI parser tests passed");
}
