#include "controls/encoder_steps.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    EncoderSteps state = {0};
    assert(encoder_steps_update(&state, 1) == 0);
    assert(encoder_steps_update(&state, 0) == 0);
    assert(encoder_steps_update(&state, 3) == 0);
    assert(encoder_steps_update(&state, 4) == 1);
    assert(encoder_steps_update(&state, 2) == 0);
    assert(encoder_steps_update(&state, 0) == -1);
    assert(encoder_steps_update(&state, 400) == 100);
    assert(encoder_steps_update(&state, 0) == -100);
    state = (EncoderSteps){.previous = 0xfffffffeu};
    assert(encoder_steps_update(&state, 2) == 1);
    assert(encoder_steps_update(&state, 0xfffffffeu) == -1);
    puts("Encoder partial detents, reverse motion, and modular count tests passed");
}
