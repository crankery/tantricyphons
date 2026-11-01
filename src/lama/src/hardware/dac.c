#include "hardware/dac.h"
#include "app/lama.h"
#include "dac.pio.h"
#include "pico/stdlib.h"
#include "pico/sem.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"

static uint16_t buffers[2][SAMPLE_BUFFER_SIZE];

static enum
{
    FREE,
    FILLING,
    READY,
    PLAYING
} states[2];

static semaphore_t free_buffers;
static PIO dac_pio = pio0;
static uint sm, dma_channel;
static int active = -1;
static unsigned next_buffer;
static bool started;
static DacDebugStats debug_stats;

/* TXSTALL is sticky: inspect once per DMA restart, before recovery. Several
 * stalls within a transfer may coalesce. Sampling only here avoids repeatedly
 * counting the same ongoing empty-FIFO stall while a buffer is being filled. */
static void observe_underrun(void)
{
    uint32_t mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    if (started && (dac_pio->fdebug & mask))
    {
        ++debug_stats.underruns;
        dac_pio->fdebug = mask;
    }
}

DacDebugStats dac_get_debug_stats(void)
{
    assert(get_core_num() == 1);
    uint32_t saved = save_and_disable_interrupts();
    DacDebugStats stats = debug_stats;
    stats.active_buffer = active;
    restore_interrupts(saved);
    return stats;
}

void dac_reset_debug_stats(void)
{
    assert(get_core_num() == 1);
    uint32_t saved = save_and_disable_interrupts();
    uint32_t budget = debug_stats.block_budget_us;
    debug_stats = (DacDebugStats){.block_budget_us = budget};
    dac_pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
    restore_interrupts(saved);
}

/* Called with interrupts disabled, or from the DMA handler on core 1.
 * A single DMA channel avoids chaining back into a buffer being rendered.
 * The joined PIO FIFO covers the interrupt handoff between blocks. */
static void start_ready_buffer(void)
{
    if (active >= 0 || states[next_buffer] != READY)
    {
        return;
    }

    observe_underrun();
    unsigned index = next_buffer;
    next_buffer ^= 1;
    states[index] = PLAYING;
    active = (int)index;
    ++debug_stats.starts[index];
    dma_channel_set_read_addr(dma_channel, buffers[index], false);
    dma_channel_set_trans_count(dma_channel, SAMPLE_BUFFER_SIZE, true);
}

static void dac_dma_handler(void)
{
    if (!dma_channel_get_irq1_status(dma_channel))
    {
        return;
    }

    dma_channel_acknowledge_irq1(dma_channel);
    assert(active >= 0);
    /* DMA has copied all words into the FIFO, so it no longer reads RAM.
     * The final samples may still be playing when the buffer is released. */
    ++debug_stats.completions[active];
    states[active] = FREE;
    active = -1;
    sem_release(&free_buffers);
    if (states[next_buffer] != READY)
    {
        ++debug_stats.late_buffers;
    }

    start_ready_buffer();
}

void dac_init(void)
{
    assert(get_core_num() == 1);
    sm = pio_claim_unused_sm(dac_pio, true);
    uint offset = pio_add_program(dac_pio, &dac_program);
    const uint pins[] = {
        DAC_CS,
        DAC_SCK,
        DAC_SDI,
        DAC_LDAC,
    };
    for (unsigned i = 0; i < 4; ++i)
    {
        pio_gpio_init(dac_pio, pins[i]);
        pio_sm_set_consecutive_pindirs(dac_pio, sm, pins[i], 1, true);
    }

    pio_sm_config config = dac_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, DAC_CS);
    sm_config_set_out_pins(&config, DAC_SDI, 1);
    sm_config_set_set_pins(&config, DAC_LDAC, 1);
    sm_config_set_out_shift(&config, false, false, 32);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
    /* Round to the closest hardware divider (8 fractional bits).
     * Actual rate = clk_sys / divider / 256. */
    uint32_t divider256 = (clock_get_hz(clk_sys) + SAMPLE_RATE / 2) / SAMPLE_RATE;
    sm_config_set_clkdiv_int_frac(&config, divider256 >> 8, divider256 & 255);
    debug_stats.block_budget_us =
        (uint32_t)((uint64_t)SAMPLE_BUFFER_SIZE * divider256 * 1000000u / clock_get_hz(clk_sys));
    pio_sm_init(dac_pio, sm, offset, &config);
    uint32_t mask = (1u << DAC_CS) | (1u << DAC_SCK) | (1u << DAC_SDI) | (1u << DAC_LDAC);
    pio_sm_set_pins_with_mask(dac_pio, sm, (1u << DAC_CS) | (1u << DAC_LDAC), mask);

    sem_init(&free_buffers, 2, 2);
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config dma_config = dma_channel_get_default_config(dma_channel);
    /* Narrow writes to the PIO FIFO replicate the halfword in both halves:
     * shifting left sends the command's bit 15 first. */
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(dac_pio, sm, true));
    dma_channel_configure(
        dma_channel, &dma_config, &dac_pio->txf[sm], NULL, SAMPLE_BUFFER_SIZE, false);
    dma_channel_acknowledge_irq1(dma_channel);
    irq_add_shared_handler(
        DMA_IRQ_1, dac_dma_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    dma_channel_set_irq1_enabled(dma_channel, true);
    irq_set_enabled(DMA_IRQ_1, true);
    /* Enable only after both initial buffers have been rendered. */
}

uint16_t *get_free_dac_buffer(void)
{
    assert(get_core_num() == 1);
    sem_acquire_blocking(&free_buffers);
    uint32_t saved = save_and_disable_interrupts();
    for (unsigned i = 0; i < 2; ++i)
    {
        if (states[i] == FREE)
        {
            states[i] = FILLING;
            restore_interrupts(saved);
            return buffers[i];
        }
    }

    panic("DAC buffer ownership mismatch");
}

void submit_dac_buffer(uint16_t *buffer)
{
    assert(get_core_num() == 1);
    uint32_t saved = save_and_disable_interrupts();
    int index = buffer == buffers[0] ? 0 : buffer == buffers[1] ? 1 : -1;
    if (index < 0 || states[index] != FILLING)
    {
        panic("Invalid DAC buffer submission");
    }

    states[index] = READY;
    if (!started)
    {
        if (states[0] == READY && states[1] == READY)
        {
            dac_pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
            started = true;
            start_ready_buffer();
            /* Prime FIFO before enabling so startup is not an underrun. */
            while (pio_sm_is_tx_fifo_empty(dac_pio, sm))
            {
                tight_loop_contents();
            }

            dac_pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm);
            pio_sm_set_enabled(dac_pio, sm, true);
        }
    }
    else
    {
        start_ready_buffer();
    }

    restore_interrupts(saved);
}
