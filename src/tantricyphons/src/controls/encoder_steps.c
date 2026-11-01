#include "controls/encoder_steps.h"

int encoder_steps_update(EncoderSteps *state, uint32_t count)
{
    uint32_t difference = count - state->previous;
    state->previous = count;
    // Decode a signed modular difference without signed integer overflow.
    int64_t delta = difference & 0x7fffffffu;
    if (difference & 0x80000000u)
    {
        delta -= 2147483648ll;
    }

    int64_t edges = delta + state->remainder;
    int steps = (int)(edges / ENCODER_EDGES_PER_DETENT);
    state->remainder = (int)(edges % ENCODER_EDGES_PER_DETENT);
    return steps;
}
