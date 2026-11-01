#include "display/monk.h"
#include <assert.h>
#include <string.h>

unsigned monk_select_frame(MonkAnimation *animation, bool active, float vowel, uint64_t now_us)
{
    if (active)
    {
        animation->was_active = true;
        if (!(vowel >= 0.0f))
        {
            vowel = 0.0f;
        }

        if (vowel > 1.0f)
        {
            vowel = 1.0f;
        }

        return 6 + (unsigned)(vowel * 23.0f + 0.5f);
    }

    if (animation->was_active)
    {
        animation->idle_since_us = now_us;
        animation->was_active = false;
    }

    uint64_t tick = (now_us - animation->idle_since_us) / 100000;
    if (tick < 48)
    {
        return ((tick >= 14 && tick < 17) || (tick >= 31 && tick < 34)) ? 2 : 5;
    }

    static const uint8_t shuffle[] = {
        5,
        3,
        4,
        3,
        2,
        1,
        0,
        1,
        5,
        3,
        4,
        3,
        5,
        1,
        0,
        1,
        2,
        3,
        4,
        3,
        5,
        1,
        0,
        1,
    };
    return shuffle[((tick - 48) / 2) % sizeof(shuffle)];
}

void monk_decode_row(unsigned frame, unsigned row, uint8_t *destination)
{
    assert(frame < 30 && row < MONK_HEIGHT);
    const uint8_t *source = &monk_frames[frame][row * (MONK_WIDTH / 2)];
    unsigned out = 0;
    for (unsigned x = 0; x < MONK_WIDTH / 2; ++x)
    {
        uint8_t packed = source[x];
        for (unsigned nibble = 0; nibble < 2; ++nibble)
        {
            unsigned index = nibble ? packed & 15 : packed >> 4;
            const uint8_t *colour = &monk_palette[index * 2];
            for (unsigned repeat = 0; repeat < 2; ++repeat)
            {
                destination[out++] = colour[0];
                destination[out++] = colour[1];
            }
        }
    }

    memcpy(destination + MONK_ROW_BYTES, destination, MONK_ROW_BYTES);
}
