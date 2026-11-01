#include "controls/cv_input.h"
#include "messaging/messenger.h"
#include "controls/calibration.h"
#include "controls/vowel_controls.h"
#include "pico/stdlib.h"

static bool pitch_valid, pitch_sent, gate_sent, last_gate;
static uint16_t pitch_raw, last_pitch;
static uint64_t last_gate_at;
static uint32_t pitch_revision;
static VowelControls vowel;

void cv_input_init(void)
{
    vowel_controls_init(&vowel);
    gpio_init(GATE_PIN);
    gpio_set_dir(GATE_PIN, GPIO_IN);
    gpio_pull_up(GATE_PIN); /* LM393 open collector, active low */
}

void cv_input_adc(unsigned channel, uint16_t raw)
{
    if (channel == 0)
    {
        pitch_raw = raw;
        pitch_valid = true;
    }

    vowel_controls_sample(&vowel, channel, raw);
}

void cv_input_poll(void)
{
    // Poll every UI pass; keep pitch and gate ordered, including startup.
    if (!pitch_valid)
    {
        return;
    }

    if (!pitch_sent || pitch_raw != last_pitch || pitch_revision != calibration_revision(CAL_PICO))
    {
        if (!add_message(MONK_SET_PITCH, cv_pitch_hz(pitch_raw)))
        {
            return;
        }

        last_pitch = pitch_raw;
        pitch_sent = true;
        pitch_revision = calibration_revision(CAL_PICO);
    }

    bool gate = !gpio_get(GATE_PIN);
    uint64_t now = time_us_64();
    vowel_controls_poll(&vowel, now);
    // Periodic status recovers CV after queue overflow. The audio-side gate
    // handler is idempotent, so a held gate does not retrigger the envelope.
    if (!gate_sent || gate != last_gate || now - last_gate_at >= 10000)
    {
        if (add_message(gate ? MONK_GATE_ON : MONK_GATE_OFF, cv_pitch_hz(pitch_raw)))
        {
            last_gate = gate;
            last_gate_at = now;
            gate_sent = true;
        }
    }
}
