#include "display/monk.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    MonkAnimation animation = {0};
    assert(monk_select_frame(&animation, false, 0, 0) == 5);
    assert(monk_select_frame(&animation, false, 0, 1400000) == 2);
    assert(monk_select_frame(&animation, false, 0, 1699999) == 2);
    assert(monk_select_frame(&animation, false, 0, 1700000) == 5);
    assert(monk_select_frame(&animation, false, 0, 3100000) == 2);
    assert(monk_select_frame(&animation, false, 0, 3400000) == 5);
    assert(monk_select_frame(&animation, false, 0, 4800000) == 5);
    assert(monk_select_frame(&animation, false, 0, 5000000) == 3);
    assert(monk_select_frame(&animation, false, 0, 9600000) == 5);
    for (unsigned frame = 0; frame < 24; ++frame)
    {
        assert(monk_select_frame(&animation, true, frame / 23.0f, 10000000) == 6 + frame);
    }

    assert(monk_select_frame(&animation, true, -1, 10000000) == 6);
    assert(monk_select_frame(&animation, true, 2, 10000000) == 29);
    assert(monk_select_frame(&animation, true, NAN, 10000000) == 6);
    assert(monk_select_frame(&animation, false, 0, 10000000) == 5);
    assert(monk_select_frame(&animation, false, 0, 11400000) == 2);

    // Verify every pixel against packed indices, including horizontal and
    // vertical duplication, RGB565 byte order, and exact output bounds.
    uint8_t guarded[2 * MONK_ROW_BYTES + 2];
    for (unsigned frame = 0; frame < 30; ++frame)
    {
        for (unsigned row = 0; row < MONK_HEIGHT; ++row)
        {
            guarded[0] = 0xa5;
            guarded[sizeof(guarded) - 1] = 0x5a;
            monk_decode_row(frame, row, guarded + 1);
            assert(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0x5a);
            for (unsigned x = 0; x < MONK_WIDTH; ++x)
            {
                uint8_t packed = monk_frames[frame][row * MONK_WIDTH / 2 + x / 2];
                unsigned index = x % 2 ? packed & 15 : packed >> 4;
                for (unsigned y_repeat = 0; y_repeat < 2; ++y_repeat)
                {
                    for (unsigned x_repeat = 0; x_repeat < 2; ++x_repeat)
                    {
                        for (unsigned byte = 0; byte < 2; ++byte)
                        {
                            unsigned offset =
                                y_repeat * MONK_ROW_BYTES + (x * 2 + x_repeat) * 2 + byte;
                            assert(guarded[1 + offset] == monk_palette[index * 2 + byte]);
                        }
                    }
                }
            }
        }
    }

    puts("Monk animation and all-frame decoding tests passed");
}
