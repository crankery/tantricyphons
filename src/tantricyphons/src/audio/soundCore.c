#include "audio/soundCore.h"
#include "messaging/messenger.h"
#include "pico/sync.h"
#include "pico/flash.h"

static MonkVoice voice;
static float buffer[SAMPLE_BUFFER_SIZE];
static float current_gain = -1.0f;
static float gain_coeff;
static float master_level = 1.0f;

static critical_section_t state_lock;
static SoundState display_state = {.vowel = 0.5f};
static AudioDebugStats published_debug;
static bool debug_reset_requested;

void sound_core_reset_debug_stats(void)
{
    critical_section_enter_blocking(&state_lock);
    debug_reset_requested = true;
    critical_section_exit(&state_lock);
}

AudioDebugStats sound_core_get_debug_stats(void)
{
    critical_section_enter_blocking(&state_lock);
    AudioDebugStats stats = published_debug;
    critical_section_exit(&state_lock);
    return stats;
}

SoundState sound_core_get_state(void)
{
    critical_section_enter_blocking(&state_lock);
    SoundState state = display_state;
    critical_section_exit(&state_lock);
    return state;
}

void soundCore();

void sound_core_start()
{
    critical_section_init(&state_lock);
    multicore_launch_core1(soundCore);
}

float gain_smoothing_coeff(float sample_rate)
{
    if (sample_rate <= 0.0f)
    {
        return 1.0f;
    }

    return 1.0f - expf(-1.0f / (0.005f * sample_rate));
}

// prepare a dac sample buffer from the voice's buffer
// the dac is an MCP4822/MCP4821
// we are targetting channel A here (for the 2 channel one)
void prepare_dac_buffer(uint16_t *dac_buffer)
{
    for (int i = 0; i < SAMPLE_BUFFER_SIZE; i++)
    {
        // take each sample, clamp to -1.0..1.0
        float sample = buffer[i];
        sample = sample > 1.0f ? 1.0f : sample < -1.0f ? -1.0f : sample;

        // push the -1 to 1 saple to dac range of 0..4096
        uint16_t dac_sample = (uint16_t)((sample + 1.0f) * 2047.5f);

        // MCP4821 command: gain = 1x, active, followed by 12 data bits.
        dac_buffer[i] = 0x3000 | dac_sample;
    }
}

void render_audio(float *buffer, uint32_t n)
{
    monk_voice_process(&voice, buffer, n);

    float target_gain = monk_voice_amplitude(&voice) * master_level;

    if (current_gain < 0.0f)
    {
        current_gain = target_gain;
    }

    for (uint32_t i = 0; i < n; i++)
    {
        current_gain += gain_coeff * (target_gain - current_gain);
        buffer[i] *= current_gain;
    }
}

void soundCoreInit()
{
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    monk_voice_init(&voice, SAMPLE_RATE);
    gain_coeff = gain_smoothing_coeff(SAMPLE_RATE);
    dac_init();
}

void soundCore()
{
    if (!flash_safe_execute_core_init())
    {
        panic("Flash lockout init failed");
    }

    soundCoreInit();
    AudioDebugStats debug = {0};
    unsigned led_buffers = 0;
    uint32_t reset_generation = 0;

    while (true)
    {
        // Acquire ownership of the next free playback buffer.
        uint16_t *dac_buffer = get_free_dac_buffer();

        critical_section_enter_blocking(&state_lock);
        bool reset_debug = debug_reset_requested;
        debug_reset_requested = false;
        critical_section_exit(&state_lock);
        if (reset_debug)
        {
            dac_reset_debug_stats();
            debug = (AudioDebugStats){.reset_generation = ++reset_generation};
        }

        uint64_t fill_started = time_us_64();
        uint32_t grains_before = voice.grain_trigger_count;
        uint32_t builds_before = voice.grain_build_count;
        process_messages(&voice, &master_level);
        uint64_t render_started = time_us_64();
        render_audio(buffer, SAMPLE_BUFFER_SIZE);
        uint64_t pack_started = time_us_64();
        prepare_dac_buffer(dac_buffer);

        submit_dac_buffer(dac_buffer);

        // Measure useful work, excluding the wait for a free buffer.
        uint64_t fill_finished = time_us_64();
        uint32_t elapsed = (uint32_t)(fill_finished - fill_started);
        debug.last_messages_us = (uint32_t)(render_started - fill_started);
        debug.last_render_us = (uint32_t)(pack_started - render_started);
        debug.last_pack_us = (uint32_t)(fill_finished - pack_started);
        debug.last_grains = voice.grain_trigger_count - grains_before;
        debug.last_grain_builds = voice.grain_build_count - builds_before;
        if (debug.last_messages_us > debug.max_messages_us)
        {
            debug.max_messages_us = debug.last_messages_us;
        }

        if (debug.last_render_us > debug.max_render_us)
        {
            debug.max_render_us = debug.last_render_us;
        }

        if (debug.last_pack_us > debug.max_pack_us)
        {
            debug.max_pack_us = debug.last_pack_us;
        }

        debug.dac = dac_get_debug_stats();
        ++debug.blocks;
        debug.last_fill_us = elapsed;
        debug.total_fill_us += elapsed;
        if (elapsed > debug.max_fill_us)
        {
            debug.max_fill_us = elapsed;
            debug.worst_messages_us = debug.last_messages_us;
            debug.worst_render_us = debug.last_render_us;
            debug.worst_pack_us = debug.last_pack_us;
            debug.worst_grains = debug.last_grains;
            debug.worst_grain_builds = debug.last_grain_builds;
        }

        if (elapsed > debug.dac.block_budget_us)
        {
            ++debug.over_budget;
        }

        SoundState state = {.vowel = voice.current_vowel,
                            .note_active = monk_voice_is_active(&voice)};
        critical_section_enter_blocking(&state_lock);
        display_state = state;
        published_debug = debug;
        critical_section_exit(&state_lock);

        // Slow audio-loop heartbeat, without delaying playback.
        if (++led_buffers == 100)
        {
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
            led_buffers = 0;
        }
    }
}
