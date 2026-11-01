/*
 * voice.h - Lama Pico 2 voice engine
 *
 * Derived from MonkSynth's dsp/voice.h by Jonathan Taylor:
 * https://github.com/JonET/monksynth
 *
 * MonkSynth credits Xavier Rodet (IRCAM) for the formant-wave-function
 * (FOF) synthesis technique, and AudioNerdz's Delay Lama (2002) as its
 * inspiration.
 *
 * Local Lama adaptations include Pico 2 integration, exact-input DSP caches,
 * rendering diagnostics, ring-buffer traversal optimizations, and formatting.
 * The original implementation is distributed under the MIT license below.
 *
 * MIT License
 *
 * Copyright (c) 2026 Jonathan Taylor
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MONK_VOICE_H
#define _MONK_VOICE_H

#include <stdbool.h>
#include <stdint.h>

#define MONK_OVERLAP_BUF_SIZE 10240
#define MONK_NUM_FORMANTS 3
#define MONK_SPLINE_TBL_SIZE 1280
#define MONK_SINE_TBL_SIZE 1024
#define MONK_MAX_GRAIN 3840 /* 192000 * 0.02: the grain is clamped to this above 192 kHz */

/* Sample rates outside this range fall back to 44.1 kHz. Zero or NaN would
 * make the vibrato phase advance by inf and spin forever; no host runs below
 * 8 kHz or above 768 kHz. The comparison is written so NaN fails it. */
#define MONK_MIN_SAMPLE_RATE 1000.0f
#define MONK_MAX_SAMPLE_RATE 768000.0f

static inline float monk_sanitize_sample_rate(float sr)
{
    return (sr >= MONK_MIN_SAMPLE_RATE && sr <= MONK_MAX_SAMPLE_RATE) ? sr : 44100.0f;
}

typedef struct
{
    float sample_rate;

    bool active;
    float current_pitch; /* MIDI note space */
    float target_pitch;
    float pitch_bend_offset; /* semitones, clamped to [-12, 12] */

    float glide_param;
    float min_glide; /* minimum glide for XY pad smoothing, independent of knob */

    /* Linear ramp state (matches original Delay Lama's 10-tick ramp) */
    float pitch_step;     /* semitones per tick */
    int pitch_ramp_ticks; /* ticks remaining */
    float vowel_step;     /* vowel units per tick */
    int vowel_ramp_ticks; /* ticks remaining */
    int ramp_counter;     /* samples until next tick */
    int ramp_period;      /* samples between ticks (sampleRate * 0.01) */

    float vibrato_phase;
    float vibrato_depth;
    float vibrato_rate; /* 0-1 normalized, scales LFO speed */
    float random_jitter;
    uint32_t jitter_counter;
    uint32_t jitter_period;
    float sr_per_vib_tbl;
    uint32_t rng_state;

    float overlap_buf[MONK_OVERLAP_BUF_SIZE];
    uint32_t overlap_write_pos;
    uint32_t overlap_read_pos;
    uint32_t overlap_offset;

    float grain[MONK_MAX_GRAIN];
    uint32_t grain_len;
    float current_vowel;
    float target_vowel;
    float current_voice;
    float aspiration_amp;
    bool grain_dirty;

    /* Exact-input caches: avoid repeating expensive math and rebuilding an
     * identical grain. Cached values are invalidated by keys/dirty state. */
    bool period_cache_valid, vibrato_cache_valid, grain_cache_valid;
    int period_cache_index;
    float period_cache_sample_rate, period_cache_value;
    float vibrato_cache_rate, vibrato_cache_scale;
    float grain_cache_vowel, grain_cache_voice, grain_cache_aspiration;
    float grain_cache_sample_rate;
    uint32_t grain_cache_length;
    uint32_t grain_build_count, grain_trigger_count, period_compute_count;

    /* ADSR envelope */
    enum
    {
        ENV_IDLE,
        ENV_ATTACK,
        ENV_DECAY,
        ENV_SUSTAIN,
        ENV_RELEASE
    } env_stage;

    float env_level;   /* current envelope value 0-1 */
    float env_attack;  /* attack time in seconds (0 = instant) */
    float env_decay;   /* decay time in seconds (0 = instant) */
    float env_sustain; /* sustain level 0-1 (1 = full) */
    float env_release; /* release time in seconds (0 = instant) */

    float sine_tbl[MONK_SINE_TBL_SIZE];
    float formant_tbl[MONK_NUM_FORMANTS][MONK_SPLINE_TBL_SIZE];
    float cos_window[MONK_MAX_GRAIN];
    float exp_decay[MONK_MAX_GRAIN * 4];
    float aspiration[MONK_MAX_GRAIN];
} MonkVoice;

void monk_voice_init(MonkVoice *v, float sample_rate);
void monk_voice_set_sample_rate(MonkVoice *v, float sample_rate);
void monk_voice_reset(MonkVoice *v);
void monk_voice_note_on(MonkVoice *v, float pitch_hz, float velocity);
void monk_voice_note_off(MonkVoice *v);
bool monk_voice_is_active(const MonkVoice *v);
void monk_voice_set_pitch_direct(MonkVoice *v, float hz);
void monk_voice_set_pitch_target(MonkVoice *v, float hz);
void monk_voice_set_vowel(MonkVoice *v, float vowel);
void monk_voice_set_voice(MonkVoice *v, float voice);
void monk_voice_set_glide(MonkVoice *v, float glide);
void monk_voice_set_vibrato(MonkVoice *v, float depth);
void monk_voice_set_vibrato_rate(MonkVoice *v, float rate);
void monk_voice_set_pitch_bend(MonkVoice *v, float semitones);
void monk_voice_set_aspiration(MonkVoice *v, float amp);
void monk_voice_set_attack(MonkVoice *v, float seconds);
void monk_voice_set_decay(MonkVoice *v, float seconds);
void monk_voice_set_sustain(MonkVoice *v, float level);
void monk_voice_set_release(MonkVoice *v, float seconds);
float monk_voice_amplitude(const MonkVoice *v);
void monk_voice_process(MonkVoice *v, float *output, uint32_t n);

float monk_note_to_hz(float note);
float monk_hz_to_note(float hz);
float monk_midi_note_to_freq(uint8_t note);

#endif // _MONK_VOICE_H
