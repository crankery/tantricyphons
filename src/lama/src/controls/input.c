#include "controls/input.h"
#include "controls/calibration.h"
#include "hardware/dac.h"
#include "mcp3208.pio.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

static PIO input_pio = pio1;
static uint sm;
static critical_section_t snapshot_lock;
static InputSnapshot pending, published;
static bool scanning, initialized, scan_valid;
static unsigned pico_channel, external_channel;
static uint64_t next_scan;
static InputUpdateHandler update_handler;

void input_set_update_handler(InputUpdateHandler handler)
{
    assert(get_core_num() == 0);
    update_handler = handler;
}

static void notify_updates(void)
{
    InputUpdateHandler handler = update_handler;
    if (!handler)
    {
        return;
    }

    for (unsigned i = 0; i < INPUT_PICO_CHANNELS; ++i)
    {
        handler(INPUT_ADC_PICO, i, pending.pico[i]);
    }

    for (unsigned i = 0; i < INPUT_EXTERNAL_CHANNELS; ++i)
    {
        handler(INPUT_ADC_MCP3208, i, pending.external[i]);
    }
}

static void start_pico_conversion(void)
{
    adc_select_input(pico_channel);
    hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);
}

static void start_external_conversion(void)
{
    /* 00000 START SGL D2 D1 D0, then don't-care bits. Transmit the
     * 24-bit frame from the top of OSR; the last 12 RX bits are data. */
    uint32_t command = (0x060000u | (external_channel << 14)) << 8;
    pio_sm_put(input_pio, sm, command);
}

void input_init(void)
{
    assert(get_core_num() == 0);
    /* Refuse to take over a live DAC pin if the board pin map conflicts. */
    const uint adc_pins[] = {
        ADC_CS,
        ADC_CLK,
        ADC_DIN,
        ADC_DOUT,
    };
    const uint dac_pins[] = {
        DAC_CS,
        DAC_SCK,
        DAC_SDI,
        DAC_LDAC,
    };
    for (unsigned i = 0; i < 4; ++i)
    {
        for (unsigned j = 0; j < 4; ++j)
        {
            if (adc_pins[i] == dac_pins[j])
            {
                panic("ADC/DAC pin conflict on GPIO %u", adc_pins[i]);
            }
        }
    }

    critical_section_init(&snapshot_lock);
    adc_init();
    for (unsigned i = 0; i < INPUT_PICO_CHANNELS; ++i)
    {
        adc_gpio_init(26 + i);
    }

    adc_set_round_robin(0);
    adc_fifo_setup(false, false, 0, false, false);

    sm = pio_claim_unused_sm(input_pio, true);
    uint offset = pio_add_program(input_pio, &mcp3208_program);
    const uint outputs[] = {
        ADC_CS,
        ADC_CLK,
        ADC_DIN,
    };
    pio_gpio_init(input_pio, ADC_DOUT);
    for (unsigned i = 0; i < 3; ++i)
    {
        pio_gpio_init(input_pio, outputs[i]);
    }

    pio_sm_config config = mcp3208_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, ADC_CS);
    sm_config_set_out_pins(&config, ADC_DIN, 1);
    sm_config_set_in_pins(&config, ADC_DOUT);
    sm_config_set_out_shift(&config, false, false, 32);
    sm_config_set_in_shift(&config, false, false, 32);
    sm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / 2000000.0f);
    pio_sm_init(input_pio, sm, offset, &config);
    for (unsigned i = 0; i < 3; ++i)
    {
        pio_sm_set_consecutive_pindirs(input_pio, sm, outputs[i], 1, true);
    }

    pio_sm_set_consecutive_pindirs(input_pio, sm, ADC_DOUT, 1, false);
    uint32_t output_mask = (1u << ADC_CS) | (1u << ADC_CLK) | (1u << ADC_DIN);
    pio_sm_set_pins_with_mask(input_pio, sm, 1u << ADC_CS, output_mask);
    pio_sm_set_enabled(input_pio, sm, true);
    initialized = true;
}

void poll_controls(void)
{
    assert(get_core_num() == 0);
    if (!initialized)
    {
        return;
    }

    uint64_t now = time_us_64();
    if (!scanning)
    {
        if (now < next_scan)
        {
            return;
        }

        scanning = true;
        scan_valid = true;
        pico_channel = external_channel = 0;
        next_scan = now + INPUT_SCAN_INTERVAL_US;
        start_pico_conversion();
        start_external_conversion();
        return;
    }

    if (pico_channel < INPUT_PICO_CHANNELS && (adc_hw->cs & ADC_CS_READY_BITS))
    {
        if (adc_hw->cs & ADC_CS_ERR_BITS)
        {
            scan_valid = false;
        }

        pending.pico[pico_channel++] = adc_hw->result & 0xfff;
        if (pico_channel < INPUT_PICO_CHANNELS)
        {
            start_pico_conversion();
        }
    }

    if (external_channel < INPUT_EXTERNAL_CHANNELS && !pio_sm_is_rx_fifo_empty(input_pio, sm))
    {
        pending.external[external_channel++] = pio_sm_get(input_pio, sm) & 0xfff;
        if (external_channel < INPUT_EXTERNAL_CHANNELS)
        {
            start_external_conversion();
        }
    }

    if (pico_channel == INPUT_PICO_CHANNELS && external_channel == INPUT_EXTERNAL_CHANNELS)
    {
        if (scan_valid)
        {
            for (unsigned i = 0; i < INPUT_PICO_CHANNELS; ++i)
            {
                pending.pico_normalized[i] = calibration_normalized(CAL_PICO, pending.pico[i]);
            }

            for (unsigned i = 0; i < INPUT_EXTERNAL_CHANNELS; ++i)
            {
                pending.external_normalized[i] =
                    calibration_normalized(CAL_MCP3208, pending.external[i]);
            }

            pending.timestamp_us = time_us_64();
            ++pending.sequence;
            critical_section_enter_blocking(&snapshot_lock);
            published = pending;
            critical_section_exit(&snapshot_lock);
            notify_updates();
        }

        scanning = false;
    }
}

bool input_get_snapshot(InputSnapshot *snapshot)
{
    if (!snapshot || !initialized)
    {
        return false;
    }

    critical_section_enter_blocking(&snapshot_lock);
    bool available = published.sequence != 0;
    if (available)
    {
        *snapshot = published;
    }

    critical_section_exit(&snapshot_lock);
    return available;
}
