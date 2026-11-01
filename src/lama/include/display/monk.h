#ifndef LAMA_MONK_H
#define LAMA_MONK_H
#include <stdbool.h>
#include <stdint.h>

#define MONK_WIDTH 120
#define MONK_HEIGHT 112
#define MONK_DISPLAY_WIDTH 240
#define MONK_DISPLAY_HEIGHT 224
#define MONK_ROW_BYTES (MONK_DISPLAY_WIDTH * 2)
#define MONK_IMAGE_BYTES (MONK_ROW_BYTES * MONK_DISPLAY_HEIGHT)
extern const uint8_t monk_palette[32];
extern const uint8_t monk_frames[30][6720];

typedef struct
{
    bool was_active;
    uint64_t idle_since_us;
} MonkAnimation;

unsigned monk_select_frame(MonkAnimation *animation, bool active, float vowel, uint64_t now_us);
/* Output is two identical 240-pixel rows in RGB565 wire byte order. */
void monk_decode_row(unsigned frame, unsigned row, uint8_t *destination);
#endif
