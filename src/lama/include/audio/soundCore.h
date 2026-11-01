#ifndef SOUND_H
#define SOUND_H

#include <stdio.h>
#include <math.h>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "app/lama.h"
#include "hardware/dac.h"
#include "audio/voice.h"

typedef struct
{
    float vowel;
    bool note_active;
} SoundState;

typedef struct
{
    DacDebugStats dac;
    uint32_t reset_generation;
    uint32_t blocks, last_fill_us, max_fill_us, over_budget;
    uint64_t total_fill_us;
    uint32_t last_messages_us, last_render_us, last_pack_us;
    uint32_t max_messages_us, max_render_us, max_pack_us;
    uint32_t last_grains, last_grain_builds;
    uint32_t worst_messages_us, worst_render_us, worst_pack_us;
    uint32_t worst_grains, worst_grain_builds;

} AudioDebugStats;

/* Coherent latest completed-block snapshot, safe on core 0 after start. */
AudioDebugStats sound_core_get_debug_stats(void);
/* Core 0 request; reset is serviced by core 1 at its next acquired buffer. */
void sound_core_reset_debug_stats(void);

/* Latest state, copied under a short lock; call after sound_core_start(). */
SoundState sound_core_get_state(void);
void sound_core_start();
float gain_smoothing_coeff(float sample_rate);
void prepare_dac_buffer(uint16_t *dac_buffer);
void render_audio(float *buffer, uint32_t n);
void soundCoreInit();

#endif // SOUND_H