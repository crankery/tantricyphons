#ifndef LAMA_DAC_H
#define LAMA_DAC_H

#include <stdint.h>

#define DAC_CS 11
#define DAC_SCK 12
#define DAC_SDI 13
#define DAC_LDAC 14

typedef struct
{
    uint32_t starts[2], completions[2];
    uint32_t late_buffers; /* Next RAM buffer not ready at DMA completion */
    uint32_t underruns;    /* Observed PIO TXSTALL episodes; sticky/coalesced */
    uint32_t block_budget_us;
    int active_buffer; /* -1 when no DMA transfer is active */
} DacDebugStats;

/* Core 1 only; audio core republishes these for core 0. */
DacDebugStats dac_get_debug_stats(void);
/* Core 1 only: clear diagnostics, preserving live DMA/PIO/buffer ownership. */
void dac_reset_debug_stats(void);

/* All calls run on core 1. Acquire blocks until a buffer is free; submit
 * transfers ownership to DMA. Never write a buffer after submitting it. */
void dac_init(void);
uint16_t *get_free_dac_buffer(void);
void submit_dac_buffer(uint16_t *buffer);

#endif
