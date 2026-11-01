#ifndef LAMA_ENCODER_STEPS_H
#define LAMA_ENCODER_STEPS_H
#include <stdint.h>
#define ENCODER_EDGES_PER_DETENT 4

typedef struct
{
    uint32_t previous;
    int remainder;
} EncoderSteps;

/* Convert a wrapping PIO edge count to detents, preserving partial/reversed
 * motion between polls. Poll before a net movement of 2^31 edges. */
int encoder_steps_update(EncoderSteps *state, uint32_t count);
#endif
