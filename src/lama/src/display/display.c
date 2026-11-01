#include "display/display.h"
#include "display/monk.h"
#include "display/menu.h"
#include "audio/soundCore.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

static uint8_t pixels[MONK_IMAGE_BYTES];
static const uint8_t black;
static uint dma_channel;
static bool initialized, transferring;

static enum
{
    RESET_LOW,
    RESET_HIGH,
    SOFTWARE_RESET,
    SLEEP_OUT,
    CONFIGURED,
    DISPLAY_ON,
    CLEARING,
    RUNNING
} phase;

static uint64_t deadline, next_frame;
static MonkAnimation animation;
static int shown_frame = -1, decoding_frame = -1;
static unsigned decode_row;
static bool rendering_menu;
static unsigned menu_row;
static uint32_t shown_menu_revision;
static MenuView menu_view;

/* Only tiny command transfers block, and never during a pixel DMA. */
static void command(uint8_t cmd, const uint8_t *data, unsigned size)
{
    gpio_put(LCD_CS, 0);
    gpio_put(LCD_DC, 0);
    spi_write_blocking(spi0, &cmd, 1);
    if (size)
    {
        gpio_put(LCD_DC, 1);
        spi_write_blocking(spi0, data, size);
    }

    gpio_put(LCD_CS, 1);
}

static void window(unsigned y, unsigned height)
{
    const uint8_t columns[] = {
        0,
        0,
        0,
        239,
    };
    const uint8_t rows[] = {
        y >> 8,
        y & 255,
        (y + height - 1) >> 8,
        (y + height - 1) & 255,
    };
    command(0x2a, columns, sizeof(columns));
    command(0x2b, rows, sizeof(rows));
    command(0x2c, NULL, 0);
}

static void transmit(const uint8_t *source, unsigned size, bool increment)
{
    dma_channel_config config = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_read_increment(&config, increment);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, spi_get_dreq(spi0, true));
    gpio_put(LCD_DC, 1);
    gpio_put(LCD_CS, 0);
    transferring = true;
    dma_channel_configure(dma_channel, &config, &spi_get_hw(spi0)->dr, source, size, true);
}

void display_init(void)
{
    assert(get_core_num() == 0);
    const uint outputs[] = {
        LCD_CS,
        LCD_DC,
        LCD_RST,
        LCD_BLK,
    };
    for (unsigned i = 0; i < 4; ++i)
    {
        gpio_init(outputs[i]);
        gpio_set_dir(outputs[i], GPIO_OUT);
    }

    gpio_put(LCD_CS, 1);
    gpio_put(LCD_RST, 0);
    gpio_put(LCD_BLK, 0);
    spi_init(spi0, DISPLAY_SPI_HZ);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MOSI, GPIO_FUNC_SPI);
    dma_channel = dma_claim_unused_channel(true);
    phase = RESET_LOW;
    deadline = time_us_64() + 20000;
    initialized = true;
}

void display_poll(void)
{
    assert(get_core_num() == 0);
    if (!initialized)
    {
        return;
    }

    if (transferring)
    {
        /* DMA complete only means the FIFO is filled. Do not release CS or
         * reuse the buffer until the SPI shifter also finishes. */
        if (dma_channel_is_busy(dma_channel) || spi_is_busy(spi0))
        {
            return;
        }

        gpio_put(LCD_CS, 1);
        /* TX-only DMA leaves received dummy bytes in the RX FIFO. */
        while (spi_is_readable(spi0))
        {
            (void)spi_get_hw(spi0)->dr;
        }

        spi_get_hw(spi0)->icr = SPI_SSPICR_RORIC_BITS;
        transferring = false;
        if (phase == CLEARING)
        {
            gpio_put(LCD_BLK, 1);
            phase = RUNNING;
            animation.idle_since_us = time_us_64();
        }
    }

    uint64_t now = time_us_64();
    if (phase != RUNNING)
    {
        if (now < deadline)
        {
            return;
        }

        switch (phase)
        {
        case RESET_LOW:
            gpio_put(LCD_RST, 1);
            phase = RESET_HIGH;
            deadline = now + 120000;
            break;
        case RESET_HIGH:
            command(0x01, NULL, 0);
            phase = SOFTWARE_RESET;
            deadline = now + 150000;
            break;
        case SOFTWARE_RESET:
            command(0x11, NULL, 0);
            phase = SLEEP_OUT;
            deadline = now + 120000;
            break;
        case SLEEP_OUT:
        {
            const uint8_t rgb565 = 0x55, portrait = 0;
            command(0x3a, &rgb565, 1);
            command(0x36, &portrait, 1);
            command(0x21, NULL, 0); /* Inversion, matching prototype default */
            command(0x13, NULL, 0);
            phase = CONFIGURED;
            deadline = now + 10000;
            break;
        }

        case CONFIGURED:
            command(0x29, NULL, 0);
            phase = DISPLAY_ON;
            deadline = now + 150000;
            break;
        case DISPLAY_ON:
            window(0, 320);
            transmit(&black, 240 * 320 * 2, false);
            phase = CLEARING;
            break;
        default:
            break;
        }

        return;
    }

    SoundState sound = sound_core_get_state();
    unsigned desired = monk_select_frame(&animation, sound.note_active, sound.vowel, now);
    if (decoding_frame < 0 && !rendering_menu)
    {
        MenuView current = menu_get_view();
        if (current.revision != shown_menu_revision)
        {
            menu_view = current;
            rendering_menu = true;
            menu_row = 0;
        }
    }

    if (rendering_menu)
    {
        menu_render_row(&menu_view, menu_row, pixels + menu_row * MENU_ROW_BYTES);
        if (++menu_row == MENU_HEIGHT)
        {
            window(MONK_DISPLAY_HEIGHT, MENU_HEIGHT);
            transmit(pixels, MENU_HEIGHT * MENU_ROW_BYTES, true);
            shown_menu_revision = menu_view.revision;
            rendering_menu = false;
        }

        return;
    }

    if (decoding_frame < 0)
    {
        if (shown_frame == (int)desired || now < next_frame)
        {
            return;
        }

        decoding_frame = (int)desired;
        decode_row = 0;
    }

    /* Bounded work per main-loop pass keeps MIDI/ADC polling responsive.
     * Keep the selected frame fixed until its complete image is ready. */
    monk_decode_row((unsigned)decoding_frame, decode_row, pixels + decode_row * MONK_ROW_BYTES * 2);
    if (++decode_row == MONK_HEIGHT)
    {
        window(0, MONK_DISPLAY_HEIGHT);
        transmit(pixels, sizeof(pixels), true);
        shown_frame = decoding_frame;
        decoding_frame = -1;
        next_frame = now + 33333; /* At most 30 new frames/sec. */
    }
}

// In particular, never erase flash while startup clear DMA reads const black.
bool display_ready_for_flash(void)
{
    return initialized && phase == RUNNING && !transferring;
}
