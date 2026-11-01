#include "voice.h"
#include <assert.h>
#include <stdio.h>

static MonkVoice voice;
static float samples[256];

static void render(unsigned blocks)
{
    for (unsigned i = 0; i < blocks; ++i)
    {
        monk_voice_process(&voice, samples, 256);
    }
}

int main(void)
{
    monk_voice_init(&voice, 44100);
    monk_voice_note_on(&voice, 440, 1);
    render(200);
    assert(voice.grain_trigger_count > 100);
    assert(voice.grain_build_count == 1);
    assert(voice.period_compute_count < 1000); // formerly one powf per sample
    uint32_t built = voice.grain_build_count;
    monk_voice_set_vowel(&voice, 0.2f);
    render(4);
    assert(voice.grain_build_count == ++built);
    monk_voice_set_voice(&voice, 0.3f);
    render(4);
    assert(voice.grain_build_count == ++built);
    monk_voice_set_aspiration(&voice, 0.1f);
    render(4);
    assert(voice.grain_build_count == ++built);
    monk_voice_set_sample_rate(&voice, 22050);
    render(4);
    assert(voice.grain_build_count == ++built);
    monk_voice_reset(&voice);
    monk_voice_note_on(&voice, 440, 1);
    render(4);
    assert(voice.grain_build_count == ++built);
    puts("Voice cache reuse and invalidation tests passed");
}
