/* Differential waveform fixture: compile against old/new voice.c and compare
 * output files. Covers stable notes, modulation, envelopes and rate changes. */
#include "voice.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        return 1;
    }

    FILE *file = fopen(argv[1], "wb");
    if (!file)
    {
        return 1;
    }

    static MonkVoice voice;
    float output[511];
    const float rates[] = {22050, 32000, 44100, 48000, 96000, 192000, 384000};
    const unsigned lengths[] = {1, 17, 64, 256, 511};
    for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate)
    {
        monk_voice_init(&voice, rates[rate]);
        monk_voice_note_on(&voice, 440, 1);
        for (unsigned block = 0; block < 300; ++block)
        {
            if (block == 40)
            {
                monk_voice_set_glide(&voice, 0.3f);
            }

            if (block == 80)
            {
                monk_voice_note_on(&voice, 220, 1);
            }

            if (block == 160)
            {
                monk_voice_note_on(&voice, 1046.5f, 1);
            }

            if (block > 90 && block % 7 == 0)
            {
                monk_voice_set_vowel(&voice, (block % 101) / 100.0f);
            }

            if (block % 31 == 0)
            {
                monk_voice_set_voice(&voice, (block % 93) / 92.0f);
            }

            if (block % 28 == 0)
            {
                monk_voice_set_aspiration(&voice, (block % 84) / 83.0f);
            }

            if (block % 53 == 0)
            {
                monk_voice_set_vibrato_rate(&voice, (block % 106) / 105.0f);
            }

            if (block == 190)
            {
                monk_voice_set_attack(&voice, 0.01f);
                monk_voice_set_decay(&voice, 0.02f);
                monk_voice_set_sustain(&voice, 0.8f);
                monk_voice_set_release(&voice, 0.05f);
            }

            if (block == 200)
            {
                monk_voice_note_off(&voice);
            }

            if (block == 208)
            {
                monk_voice_note_on(&voice, 110, 0.5f);
            }

            if (block == 240)
            {
                monk_voice_set_sample_rate(&voice, rates[rate] * 0.5f);
            }

            if (block == 260)
            {
                monk_voice_set_pitch_bend(&voice, 2.0f);
            }

            if (block == 280)
            {
                monk_voice_reset(&voice);
                monk_voice_note_on(&voice, 880, 1);
            }

            unsigned size = lengths[block % 5];
            monk_voice_process(&voice, output, size);
            if (fwrite(output, sizeof(float), size, file) != size)
            {
                return 1;
            }
        }
    }

    return fclose(file) != 0;
}
