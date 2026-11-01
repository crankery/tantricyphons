#include "audio/voice_control.h"
#include <string.h>

static bool parameter(MonkEventType type)
{
    return type <= MONK_SET_RELEASE || type == MONK_SET_GLIDE || type == MONK_SET_VOICE;
}

static void apply(MonkVoice *voice, float *level, MonkEventType type, float value)
{
    switch (type)
    {
    case MONK_SET_PITCH:
        monk_voice_set_pitch_target(voice, value);
        break;
    case MONK_SET_VOWEL:
        monk_voice_set_vowel(voice, value);
        break;
    case MONK_SET_VIBRATO:
        monk_voice_set_vibrato(voice, value);
        break;
    case MONK_SET_ASPIRATION:
        monk_voice_set_aspiration(voice, value);
        break;
    case MONK_SET_VIBRATO_RATE:
        monk_voice_set_vibrato_rate(voice, value);
        break;
    case MONK_SET_ATTACK:
        monk_voice_set_attack(voice, value);
        break;
    case MONK_SET_DECAY:
        monk_voice_set_decay(voice, value);
        break;
    case MONK_SET_SUSTAIN:
        monk_voice_set_sustain(voice, value);
        break;
    case MONK_SET_RELEASE:
        monk_voice_set_release(voice, value);
        break;
    case MONK_SET_LEVEL:
        *level = value;
        break;
    case MONK_SET_GLIDE:
        monk_voice_set_glide(voice, value);
        break;
    case MONK_SET_VOICE:
        monk_voice_set_voice(voice, value);
        break;
    default:
        break;
    }
}

static void remove_note(VoiceControl *control, uint8_t note)
{
    for (unsigned i = 0; i < control->held_count; ++i)
    {
        if (control->held[i] == note)
        {
            memmove(&control->held[i], &control->held[i + 1], control->held_count - i - 1);
            --control->held_count;
            return;
        }
    }
}

static void restore_cv(VoiceControl *control, MonkVoice *voice)
{
    if (control->cv_gate)
    {
        monk_voice_note_on(voice, control->cv_pitch, 1.0f);
    }
    else
    {
        monk_voice_note_off(voice);
    }

    monk_voice_set_pitch_target(voice, control->cv_pitch);
}

void voice_control_init(VoiceControl *control)
{
    memset(control, 0, sizeof(*control));
    control->cv_pitch = monk_note_to_hz(36.0f); // C2 at 0 V
}

void voice_control_panic(VoiceControl *control, MonkVoice *voice)
{
    control->held_count = 0;
    control->cv_gate = false;
    monk_voice_note_off(voice);
}

void voice_control_event(VoiceControl *control, MonkVoice *voice, float *level, MonkEvent event,
                         uint64_t now_us)
{
    if ((unsigned)event.type > MONK_GATE_OFF)
    {
        return;
    }

    if (event.type == MONK_SET_PITCH && event.source == MONK_SOURCE_ADC)
    {
        control->cv_pitch = event.value;
        if (!control->held_count)
        {
            apply(voice, level, event.type, event.value);
        }

        return;
    }

    if (parameter(event.type))
    {
        ParameterControl *p = &control->parameters[event.type];
        if (event.source == MONK_SOURCE_MIDI)
        {
            p->midi_value = event.value;
            p->until = now_us + MIDI_CONTROL_TIMEOUT_US;
            p->owned = true;
            p->returning = false;
            apply(voice, level, event.type, event.value);
        }
        else
        {
            if (p->returning && !p->adc_valid)
            {
                p->return_at = now_us;
            }

            p->adc_value = event.value;
            p->adc_valid = true;
            if (!p->owned && !p->returning)
            {
                apply(voice, level, event.type, event.value);
            }
        }

        return;
    }

    switch (event.type)
    {
    case MONK_NOTE_ON:
    {
        unsigned packed = (unsigned)event.value;
        if (packed >= 128 * 128)
        {
            break;
        }

        uint8_t note = packed / 128;
        remove_note(control, note);
        control->held[control->held_count++] = note;
        monk_voice_note_on(voice, monk_midi_note_to_freq(note), (packed % 128) / 127.0f);
        break;
    }

    case MONK_NOTE_OFF:
    {
        uint8_t note = (uint8_t)event.value;
        bool was_top = control->held_count && control->held[control->held_count - 1] == note;
        remove_note(control, note);
        if (!was_top)
        {
            break;
        }

        if (control->held_count)
        {
            monk_voice_note_on(
                voice, monk_midi_note_to_freq(control->held[control->held_count - 1]), 1.0f);
        }
        else
        {
            restore_cv(control, voice);
        }

        break;
    }

    case MONK_ALL_NOTES_OFF:
        control->held_count = 0;
        restore_cv(control, voice);
        break;
    case MONK_GATE_ON:
    {
        bool was_on = control->cv_gate;
        control->cv_gate = true;
        if (!control->held_count && !was_on)
        {
            control->cv_pitch = event.value;
            monk_voice_note_on(voice, control->cv_pitch, 1.0f);
        }

        break;
    }

    case MONK_GATE_OFF:
        if (control->cv_gate && !control->held_count)
        {
            monk_voice_note_off(voice);
        }

        control->cv_gate = false;
        break;
    default:
        break;
    }
}

void voice_control_tick(VoiceControl *control, MonkVoice *voice, float *level, uint64_t now_us)
{
    for (unsigned type = 0; type <= MONK_GATE_OFF; ++type)
    {
        if (!parameter((MonkEventType)type) || type == MONK_SET_PITCH)
        {
            continue;
        }

        ParameterControl *p = &control->parameters[type];
        if (p->owned && now_us >= p->until)
        {
            p->owned = false;
            p->returning = true;
            p->return_at = now_us;
        }

        if (p->returning && p->adc_valid)
        {
            float fraction = (now_us - p->return_at) / (float)CONTROL_RETURN_US;
            if (fraction >= 1.0f)
            {
                fraction = 1.0f;
                p->returning = false;
            }

            apply(voice,
                  level,
                  (MonkEventType)type,
                  p->midi_value + fraction * (p->adc_value - p->midi_value));
        }
    }
}
