#include "diagnostics/audio_debug.h"
#if TANTRICYPHONS_AUDIO_DEBUG
#include "audio/soundCore.h"
#include "hardware/uart.h"
#include <inttypes.h>

static char output[256];
static unsigned output_size, output_position;

void audio_debug_init(void)
{
    uart_init(uart0, DEBUG_UART_BAUD);
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);
    gpio_set_function(DEBUG_UART_TX_PIN, GPIO_FUNC_UART);
    // TX only: GP1 remains the LCD chip select. MIDI stays on UART1.
}

void audio_debug_poll(void)
{
    // Bounded, nonblocking output. A full TX FIFO simply waits for a later
    // main-loop pass; no logging runs on the audio core or in an IRQ.
    for (unsigned i = 0; i < 8 && output_position < output_size; ++i)
    {
        if (!uart_is_writable(uart0))
        {
            break;
        }

        uart_get_hw(uart0)->dr = (uint8_t)output[output_position++];
    }

    static uint64_t next_report;
    uint64_t now = time_us_64();
    if (now < next_report)
    {
        return;
    }

    next_report = now + 1000000;
    if (output_position < output_size)
    {
        return;
    }

    AudioDebugStats stats = sound_core_get_debug_stats();
    if (!stats.blocks)
    {
        return;
    }

    uint32_t average = (uint32_t)(stats.total_fill_us / stats.blocks);
    // Prepare at most one summary per second; transmission is incremental.
    int size = snprintf(output,
                        sizeof(output),
                        "audio blocks=%" PRIu32 " A/B=%" PRIu32 "/%" PRIu32 " late=%" PRIu32
                        " underrun=%" PRIu32 " fill(us) last/avg/max=%" PRIu32 "/%" PRIu32
                        "/%" PRIu32 " budget=%" PRIu32 " slow=%" PRIu32 " dma=%d\n",
                        stats.blocks,
                        stats.dac.completions[0],
                        stats.dac.completions[1],
                        stats.dac.late_buffers,
                        stats.dac.underruns,
                        stats.last_fill_us,
                        average,
                        stats.max_fill_us,
                        stats.dac.block_budget_us,
                        stats.over_budget,
                        stats.dac.active_buffer);
    output_size = size <= 0                         ? 0
                  : (unsigned)size < sizeof(output) ? (unsigned)size
                                                    : sizeof(output) - 1;
    output_position = 0;
}
#else
void audio_debug_init(void)
{
}

void audio_debug_poll(void)
{
}
#endif
