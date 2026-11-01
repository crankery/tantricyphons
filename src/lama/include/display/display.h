#ifndef LAMA_DISPLAY_H
#define LAMA_DISPLAY_H
#include <stdbool.h>

#define LCD_CS 1
#define LCD_SCK 2
#define LCD_MOSI 3
#define LCD_DC 4
#define LCD_RST 5
#define LCD_BLK 6
/* Conservative bring-up setting; actual SPI rate depends on clk_peri. */
#define DISPLAY_SPI_HZ 12500000

/* Core 0 only. Init starts an asynchronous panel reset; poll every UI loop.
 * ST7789, portrait 240x320, RGB565, zero panel offsets. */
void display_init(void);
void display_poll(void);
bool display_ready_for_flash(void);
#endif
