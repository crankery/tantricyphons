#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"

#include "display/display.h"
#include "controls/input.h"
#include "app/tantricyphons.h"
#include "audio/soundCore.h"
#include "midi/midi.h"
#include "messaging/messenger.h"
#include "display/menu.h"
#include "controls/routing.h"
#include "controls/ui_controls.h"
#include "controls/cv_input.h"
#include "diagnostics/audio_debug.h"
#include "settings/settings.h"
#include "controls/calibration.h"
#include "diagnostics/usb_console.h"

static void adc_updated(InputAdc adc, unsigned channel, uint16_t value)
{
    if (adc == INPUT_ADC_PICO)
    {
        cv_input_adc(channel, value);
        if (channel == 0)
        {
            calibration_sample(CAL_PICO, value);
        }
    }

    if (adc == INPUT_ADC_MCP3208 && channel == 4)
    {
        calibration_sample(CAL_MCP3208, value);
    }

    if (adc == INPUT_ADC_MCP3208)
    {
        routing_update(channel, value, time_us_64());
    }
}

int main()
{
    // Diagnostics use TX-only UART0 to the debug probe.
    stdio_init_all();
    audio_debug_init();

    start_messenger();
    menu_init();
    settings_load();
    ui_controls_init();
    midi_init();
    cv_input_init();
    input_set_update_handler(adc_updated);
    input_init();

    // start up the sound core
    sound_core_start();
    display_init();
    usb_console_init();

    // UI loop
    while (true)
    {
        midi_poll();
        ui_controls_poll();
        poll_controls();
        cv_input_poll();
        menu_poll();
        display_poll();
        settings_poll(display_ready_for_flash());
        audio_debug_poll();
        usb_console_poll();
        tight_loop_contents();
    }
}