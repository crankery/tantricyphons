#include "display/menu.h"
#include "messaging/messenger.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static MonkEvent last;
static unsigned messages;
static bool accept = true;

bool add_message(MonkEventType type, float value)
{
    if (!accept)
    {
        return false;
    }

    last = (MonkEvent){.type = type, .value = value};
    ++messages;
    return true;
}

int main(void)
{
    menu_init();
    settings_load();
    menu_press(); // enter Input assignments from Setup
    MenuView view = menu_get_view();
    assert(view.open && !view.editing && view.selected == 0);
    for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
    {
        assert(routing_get_target(i) == TARGET_OFF);
        routing_update(i, 2048, 1000);
    }

    assert(messages == 0);
    menu_press();
    menu_turn(TARGET_VOWEL);
    assert(menu_get_view().draft == TARGET_VOWEL);
    assert(routing_get_target(0) == TARGET_OFF); // no live preview
    menu_back();
    assert(!menu_get_view().editing && routing_get_target(0) == TARGET_OFF);
    menu_press();
    menu_turn(TARGET_VOWEL);
    menu_press();
    assert(routing_get_target(0) == TARGET_VOWEL);
    routing_update(0, 4095, 1000);
    assert(messages == 1 && last.type == MONK_SET_VOWEL && last.value == 1.0f);
    routing_update(0, 4090, 25000); // ADC jitter below threshold
    assert(messages == 1);
    routing_update(0, 0, 2000); // throttle retains latest reading on next scan
    assert(messages == 1);
    routing_update(0, 0, 21000);
    assert(messages == 2 && last.value == 0);
    routing_update(0, 4095, 41000);
    assert(messages == 3);
    routing_set_target(0, TARGET_RELEASE);
    accept = false;
    routing_update(0, 4095, 41001);
    assert(messages == 3);
    accept = true;
    routing_update(0, 4095, 41002); // failed enqueue must be retried
    assert(messages == 4 && last.type == MONK_SET_RELEASE && last.value == 4.0f);
    routing_set_target(0, TARGET_PITCH);
    routing_update(0, 0, 41003);
    assert(last.type == MONK_SET_PITCH && fabsf(last.value - 65.4064f) < 0.01f);
    routing_update(0, 4095, 61003);
    assert(fabsf(last.value - 1046.502f) < 0.01f);
    routing_set_target(0, TARGET_OFF);
    unsigned before = messages;
    routing_update(0, 2048, 90000);
    assert(messages == before);
    menu_turn(4);
    assert(menu_get_view().selected == 4);
    menu_press();
    menu_turn(-1); // wraps OFF backward to RELEASE
    assert(menu_get_view().draft == TARGET_RELEASE);
    menu_press();
    assert(routing_get_target(4) == TARGET_RELEASE);
    menu_turn(127);
    assert(menu_get_view().selected == 7);
    menu_turn(-127);
    assert(menu_get_view().selected == 0);
    menu_back(); // assignments -> Setup
    menu_back();
    assert(!menu_get_view().open);
    menu_press();
    assert(menu_get_view().open);
    menu_press(); // Setup -> assignments

    // Render both pages in browse/edit mode; check output boundaries and
    // save a representative screen for visual inspection.
    FILE *image = fopen("/tmp/lama-menu.ppm", "wb");
    assert(image);
    fprintf(image, "P6\n240 96\n255\n");
    uint8_t row[MENU_ROW_BYTES + 2];
    view = menu_get_view();
    for (unsigned y = 0; y < MENU_HEIGHT; ++y)
    {
        row[0] = 0xa5;
        row[sizeof(row) - 1] = 0x5a;
        menu_render_row(&view, y, row + 1);
        assert(row[0] == 0xa5 && row[sizeof(row) - 1] == 0x5a);
        for (unsigned x = 0; x < 240; ++x)
        {
            unsigned color = row[1 + x * 2] * 256 + row[2 + x * 2];
            uint8_t rgb[] = {
                (color >> 11) * 255 / 31, ((color >> 5) & 63) * 255 / 63, (color & 31) * 255 / 31};
            fwrite(rgb, 1, 3, image);
        }
    }

    fclose(image);
    menu_turn(4);
    menu_press();
    view = menu_get_view();
    for (unsigned y = 0; y < MENU_HEIGHT; ++y)
    {
        menu_render_row(&view, y, row + 1);
    }

    menu_back();
    menu_back();
    view = menu_get_view();
    for (unsigned y = 0; y < MENU_HEIGHT; ++y)
    {
        menu_render_row(&view, y, row + 1);
    }

    assert(menu_get_view().page == MENU_MAIN);
    menu_turn(1);
    menu_press();
    assert(menu_get_view().page == MENU_CAL_BANK);
    menu_press();
    assert(menu_get_view().page == MENU_CAL_1V);
    menu_press();
    assert(menu_get_view().page == MENU_CAL_WAIT_1V);
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, 850);
    }

    menu_poll();
    assert(menu_get_view().page == MENU_CAL_4V);
    menu_press();
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, 3300);
    }

    menu_poll();
    assert(menu_get_view().page == MENU_CAL_REVIEW);
    assert(calibration_get(CAL_PICO).raw_1v == 819); // not applied before OK
    menu_back();
    assert(calibration_get(CAL_PICO).raw_1v == 819); // cancel preserves existing calibration
    menu_press();
    menu_press();
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, 850);
    }

    menu_poll();
    menu_press();
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, 3300);
    }

    menu_poll();
    menu_press();
    assert(menu_get_view().page == MENU_CAL_DONE && calibration_get(CAL_PICO).raw_1v == 850);
    menu_press();
    menu_turn(1);
    menu_press();
    assert(menu_get_view().page == MENU_SAVE_CONFIRM);
    menu_back();
    assert(settings_status() != SETTINGS_PENDING);
    menu_press();
    menu_press();
    assert(settings_status() == SETTINGS_PENDING);
    menu_back();
    assert(menu_get_view().page == MENU_SAVE_RESULT); // wait for result
    settings_poll(true);
    menu_poll();
    assert(settings_status() == SETTINGS_SAVED && !settings_dirty());
    menu_press();
    menu_turn(1);
    menu_press();
    assert(menu_get_view().page == MENU_RESET_CONFIRM);
    menu_press();
    assert(calibration_get(CAL_PICO).raw_1v == 819 && settings_dirty());
    menu_press(); // reset done -> Setup
    menu_turn(-2);
    menu_press();
    assert(menu_get_view().page == MENU_CAL_BANK);
    menu_turn(1);
    assert(menu_get_view().calibration_bank == CAL_MCP3208);
    menu_press();
    menu_press();
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_PICO, 900);
    }

    menu_poll();
    assert(menu_get_view().page == MENU_CAL_WAIT_1V);
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_MCP3208, 2500);
    }

    menu_poll();
    menu_press();
    for (unsigned i = 0; i < 64; ++i)
    {
        calibration_sample(CAL_MCP3208, 3720);
    }

    menu_poll();
    assert(menu_get_view().page == MENU_CAL_REVIEW);
    menu_press();
    assert(calibration_get(CAL_PICO).raw_1v == 819);
    assert(calibration_get(CAL_MCP3208).raw_1v == 2500);
    routing_set_target(5, TARGET_LEVEL);
    routing_update(5, 2500, 100000);
    assert(last.type == MONK_SET_LEVEL && fabsf(last.value - 0.6f) < 0.0001f);
    unsigned before_calibration = messages;
    assert(calibration_set(CAL_MCP3208, (AdcCalibration){2520, 3740}));
    routing_update(5, 2500, 100001); // unchanged ADC must refresh after calibration
    assert(messages == before_calibration + 1 && last.value < 0.6f);
    menu_poll();
    uint32_t revision_before_cli = menu_get_view().revision;
    routing_set_target(6, TARGET_VOICE); // same path as console assign
    menu_poll();
    assert(menu_get_view().revision > revision_before_cli);
    assert(menu_get_view().assignments[6] == TARGET_VOICE);
    // Bounds-check every setup/calibration/save page, including empty rows.
    for (unsigned page = MENU_MAIN; page <= MENU_RESET_DONE; ++page)
    {
        view = menu_get_view();
        view.page = (MenuPage)page;
        for (unsigned y = 0; y < MENU_HEIGHT; ++y)
        {
            row[0] = 0xa5;
            row[sizeof(row) - 1] = 0x5a;
            menu_render_row(&view, y, row + 1);
            assert(row[0] == 0xa5 && row[sizeof(row) - 1] == 0x5a);
        }
    }

    puts("Menu navigation, routing, calibration wizard, save, and rendering tests passed");
}
