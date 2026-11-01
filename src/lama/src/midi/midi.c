#include "midi/midi.h"
#include "messaging/messenger.h"

/* Source-tagged MIDI events receive priority in the audio-side arbiter. */
static uint8_t status, first, count;

void midi_reset_parser(void)
{
    status = count = 0;
}

void midi_receive_byte(uint8_t byte)
{
    /* Real-time messages may occur anywhere, including within SysEx. */
    if (byte >= 0xf8)
    {
        if (byte == 0xff)
        {
            midi_reset_parser();
            add_midi_message(MONK_ALL_NOTES_OFF, 0);
        }

        return;
    }

    if (byte & 0x80)
    {
        count = 0;
        /* System common and SysEx cancel running status. Their data is ignored. */
        status = byte < 0xf0 ? byte : 0;
        return;
    }

    if (!status)
    {
        return;
    }

    uint8_t kind = status & 0xf0;
    if (kind == 0xc0 || kind == 0xd0)
    {
        return; /* Unsupported one-byte channel messages. */
    }

    if (count++ == 0)
    {
        first = byte;
        return;
    }

    count = 0;
    if ((status & 0x0f) != MIDI_CHANNEL - 1)
    {
        return;
    }

    switch (kind)
    {
    case 0x80:
        add_midi_message(MONK_NOTE_OFF, first);
        break;
    case 0x90:
        if (byte == 0)
        {
            add_midi_message(MONK_NOTE_OFF, first);
        }
        else
        {
            add_midi_message(MONK_NOTE_ON, first * 128 + byte);
        }

        break;
    case 0xb0:
        switch (first)
        {
        case 1:
            add_midi_message(MONK_SET_VIBRATO, byte / 127.0f);
            break;
        case 5:
            add_midi_message(MONK_SET_GLIDE, byte / 127.0f);
            break;
        case 7:
            add_midi_message(MONK_SET_LEVEL, byte / 127.0f);
            break;
        case 13:
            add_midi_message(MONK_SET_VOICE, byte / 127.0f);
            break;
        case 120:
        case 123:
            add_midi_message(MONK_ALL_NOTES_OFF, 0);
            break;
        }

        break;
    case 0xe0:
        /* Classic Monksynth mapping: wheel controls vowel, center = 0.5. */
        add_midi_message(MONK_SET_VOWEL, (first + byte * 128) / 16384.0f);
        break;
    }
}
