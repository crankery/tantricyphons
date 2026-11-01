#include "controls/ui_controls.h"
#include "display/menu.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "encoder.pio.h"
#include "controls/encoder_steps.h"
#include "pico/stdlib.h"

_Static_assert(ENCODER_B == ENCODER_A + 1, "PIO encoder pins must be consecutive");

static PIO encoder_pio = pio2;
static uint encoder_sm;
static EncoderSteps encoder_steps;
static uint64_t next_encoder_poll;

static struct Button
{
    uint pin;
    bool raw, stable;
    uint64_t changed;
} buttons[2];

void ui_controls_init(void)
{
    const uint pins[] = {
        ENCODER_A,
        ENCODER_B,
        ENCODER_SW,
        BACK_SW,
    };
    for (unsigned i = 0; i < 4; ++i)
    {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }

    encoder_sm = pio_claim_unused_sm(encoder_pio, true);
    uint offset = pio_add_program(encoder_pio, &encoder_program);
    pio_gpio_init(encoder_pio, ENCODER_A);
    pio_gpio_init(encoder_pio, ENCODER_B);
    pio_sm_config config = encoder_program_get_default_config(offset);
    sm_config_set_in_pins(&config, ENCODER_A);
    sm_config_set_in_shift(&config, false, false, 32);
    sm_config_set_out_shift(&config, true, false, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / 1000000.0f);
    pio_sm_init(encoder_pio, encoder_sm, offset + encoder_offset_update, &config);
    pio_sm_set_consecutive_pindirs(encoder_pio, encoder_sm, ENCODER_A, 2, false);
    // Seed previous pin state from the real inputs, avoiding a startup edge.
    pio_sm_exec(encoder_pio, encoder_sm, pio_encode_set(pio_y, 0));
    pio_sm_exec(encoder_pio, encoder_sm, pio_encode_mov(pio_isr, pio_null));
    pio_sm_exec(encoder_pio, encoder_sm, pio_encode_in(pio_pins, 2));
    pio_sm_exec(encoder_pio, encoder_sm, pio_encode_mov(pio_osr, pio_isr));
    pio_sm_set_enabled(encoder_pio, encoder_sm, true);
    buttons[0] = (struct Button){
        .pin = ENCODER_SW, .raw = gpio_get(ENCODER_SW), .stable = gpio_get(ENCODER_SW)};
    buttons[1] =
        (struct Button){.pin = BACK_SW, .raw = gpio_get(BACK_SW), .stable = gpio_get(BACK_SW)};
}

void ui_controls_poll(void)
{
    uint64_t now = time_us_64();
    if (now >= next_encoder_poll)
    {
        next_encoder_poll = now + 1000;
        uint32_t count = encoder_steps.previous;
        bool received = false;
        // Bounded drain: never wait for PIO to produce a fresh sample. A full
        // FIFO contains stale snapshots, but the PIO accumulator keeps counting.
        // A later poll picks up any movement after those snapshots.
        for (unsigned i = 0; i < 8 && !pio_sm_is_rx_fifo_empty(encoder_pio, encoder_sm); ++i)
        {
            count = pio_sm_get(encoder_pio, encoder_sm);
            received = true;
        }

        if (received)
        {
            menu_turn(encoder_steps_update(&encoder_steps, count));
        }
    }

    for (unsigned i = 0; i < 2; ++i)
    {
        bool raw = gpio_get(buttons[i].pin);
        if (raw != buttons[i].raw)
        {
            buttons[i].raw = raw;
            buttons[i].changed = now;
        }

        if (raw != buttons[i].stable && now - buttons[i].changed >= 20000)
        {
            buttons[i].stable = raw;
            if (!raw)
            {
                if (i == 0)
                {
                    menu_press();
                }
                else
                {
                    menu_back();
                }
            }
        }
    }
}

uint32_t ui_controls_encoder_count(void)
{
    return encoder_steps.previous;
}
