#include "menu.h"
#include "menu_font.h"
#include <stdio.h>
#include <string.h>

static MenuView menu;
static unsigned home_selection;
static bool last_dirty;
static SettingsStatus last_status;
static const char *home[] = {
    "INPUT ASSIGNMENTS",
    "ADC CALIBRATION",
    "SAVE SETTINGS",
    "RESET DEFAULTS",
};

void menu_init(void)
{
    routing_init();
    calibration_defaults();
    memset(&menu, 0, sizeof(menu));
    home_selection = 0;
    last_dirty = false;
    last_status = SETTINGS_IDLE;
    menu.open = true;
    menu.revision = 1;
}

static void go_home(void)
{
    calibration_capture_cancel();
    menu.page = MENU_MAIN;
    menu.selected = home_selection;
    menu.editing = false;
}

void menu_turn(int steps)
{
    if (!menu.open || !steps)
    {
        return;
    }

    if (menu.editing)
    {
        int next = ((int)menu.draft + steps % TARGET_COUNT) % TARGET_COUNT;
        if (next < 0)
        {
            next += TARGET_COUNT;
        }

        menu.draft = (RoutingTarget)next;
    }
    else if (menu.page == MENU_MAIN || menu.page == MENU_ASSIGNMENTS)
    {
        int limit = menu.page == MENU_MAIN ? 4 : ROUTING_INPUTS;
        int next = (int)menu.selected + (steps > 128 ? 128 : steps < -128 ? -128 : steps);
        if (next < 0)
        {
            next = 0;
        }

        if (next >= limit)
        {
            next = limit - 1;
        }

        if ((unsigned)next == menu.selected)
        {
            return;
        }

        menu.selected = (unsigned)next;
        if (menu.page == MENU_MAIN)
        {
            home_selection = menu.selected;
        }
    }
    else if (menu.page == MENU_CAL_BANK)
    {
        int next = (int)menu.calibration_bank + (steps > 0 ? 1 : -1);
        if (next < 0)
        {
            next = 0;
        }

        if (next >= CAL_BANKS)
        {
            next = CAL_BANKS - 1;
        }

        if ((CalibrationBank)next == menu.calibration_bank)
        {
            return;
        }

        menu.calibration_bank = (CalibrationBank)next;
    }
    else
    {
        return;
    }

    ++menu.revision;
}

void menu_press(void)
{
    if (!menu.open)
    {
        menu.open = true;
        go_home();
    }
    else
    {
        switch (menu.page)
        {
        case MENU_MAIN:
            switch (menu.selected)
            {
            case 0:
                menu.page = MENU_ASSIGNMENTS;
                menu.selected = 0;
                break;
            case 1:
                menu.page = MENU_CAL_BANK;
                break;
            case 2:
                menu.page = MENU_SAVE_CONFIRM;
                break;
            case 3:
                menu.page = MENU_RESET_CONFIRM;
                break;
            }

            break;
        case MENU_ASSIGNMENTS:
            if (!menu.editing)
            {
                menu.draft = routing_get_target(menu.selected);
                menu.editing = true;
            }
            else
            {
                routing_set_target(menu.selected, menu.draft);
                menu.editing = false;
            }

            break;
        case MENU_CAL_BANK:
            menu.page = MENU_CAL_1V;
            break;
        case MENU_CAL_1V:
            calibration_capture_begin(menu.calibration_bank);
            menu.page = MENU_CAL_WAIT_1V;
            break;
        case MENU_CAL_4V:
            calibration_capture_begin(menu.calibration_bank);
            menu.page = MENU_CAL_WAIT_4V;
            break;
        case MENU_CAL_REVIEW:
            menu.page = calibration_set(menu.calibration_bank, menu.calibration) ? MENU_CAL_DONE
                                                                                 : MENU_CAL_FAILED;
            break;
        case MENU_CAL_FAILED:
            menu.page = MENU_CAL_1V;
            break;
        case MENU_SAVE_CONFIRM:
            settings_request_save();
            menu.page = MENU_SAVE_RESULT;
            break;
        case MENU_SAVE_RESULT:
            if (settings_status() == SETTINGS_PENDING)
            {
                return;
            }

            go_home();
            break;
        case MENU_RESET_CONFIRM:
            settings_restore_defaults();
            menu.page = MENU_RESET_DONE;
            break;
        case MENU_CAL_DONE:
        case MENU_RESET_DONE:
            go_home();
            break;
        default:
            return; // capture in progress
        }
    }

    ++menu.revision;
}

void menu_back(void)
{
    if (!menu.open)
    {
        return;
    }

    if (menu.page == MENU_SAVE_RESULT && settings_status() == SETTINGS_PENDING)
    {
        return;
    }

    if (menu.editing)
    {
        menu.editing = false;
    }
    else if (menu.page == MENU_MAIN)
    {
        menu.open = false;
    }
    else if (menu.page >= MENU_CAL_1V && menu.page <= MENU_CAL_DONE)
    {
        calibration_capture_cancel();
        menu.page = MENU_CAL_BANK;
    }
    else
    {
        go_home();
    }

    ++menu.revision;
}

void menu_poll(void)
{
    bool assignments_changed = false;
    for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
    {
        RoutingTarget target = routing_get_target(i);
        if (menu.assignments[i] != target)
        {
            menu.assignments[i] = target;
            assignments_changed = true;
        }
    }

    if (assignments_changed)
    {
        ++menu.revision;
    }

    if (menu.page == MENU_CAL_WAIT_1V || menu.page == MENU_CAL_WAIT_4V)
    {
        float raw;
        bool success;
        if (calibration_capture_result(&raw, &success))
        {
            calibration_capture_cancel();
            if (!success)
            {
                menu.page = MENU_CAL_FAILED;
            }
            else if (menu.page == MENU_CAL_WAIT_1V)
            {
                menu.calibration.raw_1v = raw;
                menu.page = MENU_CAL_4V;
            }
            else
            {
                menu.calibration.raw_4v = raw;
                menu.page = calibration_valid(menu.calibration_bank, menu.calibration)
                                ? MENU_CAL_REVIEW
                                : MENU_CAL_FAILED;
            }

            ++menu.revision;
        }
    }

    bool dirty = settings_dirty();
    SettingsStatus status = settings_status();
    if (dirty != last_dirty || status != last_status)
    {
        last_dirty = dirty;
        last_status = status;
        ++menu.revision;
    }
}

MenuView menu_get_view(void)
{
    MenuView view = menu;
    for (unsigned i = 0; i < ROUTING_INPUTS; ++i)
    {
        view.assignments[i] = routing_get_target(i);
    }

    view.dirty = settings_dirty();
    view.save_status = settings_status();
    return view;
}

static const char *heading(const MenuView *view)
{
    MenuPage page = view->page;
    if (page == MENU_MAIN)
    {
        return "SETUP";
    }

    if (page == MENU_ASSIGNMENTS)
    {
        return "INPUT ASSIGNMENTS";
    }

    if (page == MENU_CAL_BANK)
    {
        return "ADC CALIBRATION";
    }

    if (page >= MENU_CAL_1V && page <= MENU_CAL_DONE)
    {
        return view->calibration_bank == CAL_PICO ? "PICO ADC CALIBRATION" : "MCP3208 CALIBRATION";
    }

    if (page == MENU_SAVE_CONFIRM || page == MENU_SAVE_RESULT)
    {
        return "SAVE SETTINGS";
    }

    return "RESET DEFAULTS";
}

static void body(const MenuView *view, unsigned line, char text[31])
{
    const char *s = "";
    switch (view->page)
    {
    case MENU_CAL_1V:
    case MENU_CAL_4V:
        if (line == 0)
        {
            snprintf(text,
                     31,
                     "APPLY %s V TO %s",
                     view->page == MENU_CAL_1V ? "1.000" : "4.000",
                     view->calibration_bank == CAL_PICO ? "FREQ CV" : "CV 1");
            return;
        }

        if (line == 1)
        {
            s = "LET VOLTAGE SETTLE";
        }

        if (line == 2)
        {
            s = "OK TO CAPTURE 64 SAMPLES";
        }

        break;
    case MENU_CAL_WAIT_1V:
    case MENU_CAL_WAIT_4V:
        if (line == 0)
        {
            s = "MEASURING...";
        }

        if (line == 1)
        {
            s = "KEEP VOLTAGE STEADY";
        }

        break;
    case MENU_CAL_REVIEW:
        if (line == 0)
        {
            snprintf(text, 31, "1V ADC: %u", (unsigned)(view->calibration.raw_1v + 0.5f));
        }

        if (line == 1)
        {
            snprintf(text, 31, "4V ADC: %u", (unsigned)(view->calibration.raw_4v + 0.5f));
        }

        if (line == 2)
        {
            s = "OK TO USE CALIBRATION";
        }

        if (line < 2)
        {
            return;
        }

        break;
    case MENU_CAL_FAILED:
        if (line == 0)
        {
            s = "CAPTURE INVALID OR NOISY";
        }

        if (line == 1)
        {
            s = "CHECK INPUT / VOLTAGE";
        }

        if (line == 2)
        {
            s = "OK TO TRY AGAIN";
        }

        break;
    case MENU_CAL_DONE:
        if (line == 0)
        {
            s = "CALIBRATION APPLIED";
        }

        if (line == 1)
        {
            s = "USE SAVE SETTINGS TO KEEP";
        }

        break;
    case MENU_SAVE_CONFIRM:
        if (line == 0)
        {
            s = "SAVE ALL ASSIGNMENTS";
        }

        if (line == 1)
        {
            s = "AND ADC CALIBRATIONS?";
        }

        if (line == 2)
        {
            s = "AUDIO MAY PAUSE BRIEFLY";
        }

        break;
    case MENU_SAVE_RESULT:
        if (line == 0)
        {
            s = view->save_status == SETTINGS_PENDING ? "SAVING..."
                : view->save_status == SETTINGS_SAVED ? "SETTINGS SAVED"
                                                      : "SAVE FAILED";
        }

        if (line == 1 && view->save_status == SETTINGS_ERROR)
        {
            s = "TRY SAVE AGAIN";
        }

        break;
    case MENU_RESET_CONFIRM:
        if (line == 0)
        {
            s = "RESET ASSIGNMENTS AND";
        }

        if (line == 1)
        {
            s = "ADC CALIBRATIONS?";
        }

        if (line == 2)
        {
            s = "OK TO RESET / BACK CANCEL";
        }

        break;
    case MENU_RESET_DONE:
        if (line == 0)
        {
            s = "DEFAULTS LOADED";
        }

        if (line == 1)
        {
            s = "USE SAVE SETTINGS TO KEEP";
        }

        break;
    default:
        break;
    }

    snprintf(text, 31, "%s", s);
}

void menu_render_row(const MenuView *view, unsigned row, uint8_t *pixels)
{
    char text[31] = "";
    unsigned glyph_row = 0;
    uint16_t foreground = 0xffff, background = 0x0841;
    if (row >= MENU_HEIGHT)
    {
        return;
    }

    if (!view->open)
    {
        if (row < 8)
        {
            strcpy(text, "OK: SETUP");
            glyph_row = row;
        }
        else if (row >= 24 && row < 32)
        {
            strcpy(text, view->dirty ? "UNSAVED CHANGES" : "POTS / CV / CALIBRATION");
            glyph_row = row - 24;
        }
    }
    else if (row < 8)
    {
        if (view->page == MENU_ASSIGNMENTS)
        {
            snprintf(text, sizeof(text), "INPUT ASSIGNMENTS         %u/2", view->selected / 4 + 1);
        }
        else
        {
            snprintf(text, sizeof(text), "%s%s", heading(view), view->dirty ? " *" : "");
        }

        glyph_row = row;
    }
    else if (row >= 16 && row < 80)
    {
        unsigned line = (row - 16) / 16;
        bool selected = false;
        if (view->page == MENU_ASSIGNMENTS)
        {
            unsigned input = view->selected / 4 * 4 + line;
            selected = input == view->selected;
            RoutingTarget target =
                selected && view->editing ? view->draft : view->assignments[input];
            snprintf(text,
                     sizeof(text),
                     "%c %-5s : %s",
                     selected ? '>' : ' ',
                     routing_input_name(input),
                     routing_target_name(target));
        }
        else if (view->page == MENU_CAL_BANK)
        {
            if (line < CAL_BANKS)
            {
                selected = line == (unsigned)view->calibration_bank;
                snprintf(text,
                         sizeof(text),
                         "%c %s",
                         selected ? '>' : ' ',
                         line == CAL_PICO ? "PICO ADC (FREQ CV)" : "MCP3208 (CV 1)");
            }
        }
        else if (view->page == MENU_MAIN)
        {
            selected = line == view->selected;
            snprintf(text, sizeof(text), "%c %s", selected ? '>' : ' ', home[line]);
        }
        else
        {
            body(view, line, text);
        }

        glyph_row = ((row - 16) % 16) / 2;
        if (selected)
        {
            background = view->editing ? 0xffc0 : 0x05ff;
            foreground = 0;
        }
    }
    else if (row >= 88)
    {
        const char *footer = "OK:CONTINUE  BACK:CANCEL";
        if (view->page == MENU_MAIN)
        {
            footer = "OK:OPEN  BACK:CLOSE";
        }
        else if (view->page == MENU_ASSIGNMENTS)
        {
            footer = view->editing ? "OK:ACCEPT  BACK:CANCEL" : "OK:EDIT  BACK:SETUP";
        }

        strcpy(text, footer);
        glyph_row = row - 88;
    }

    size_t length = strlen(text);
    for (unsigned x = 0; x < 240; ++x)
    {
        unsigned character = x / 8;
        bool on = false;
        if (character < length)
        {
            unsigned c = (unsigned char)text[character];
            if (c >= 32 && c < 128)
            {
                on = (menu_font[c - 32][glyph_row] & (0x80u >> (x % 8))) != 0;
            }
        }

        uint16_t color = on ? foreground : background;
        pixels[x * 2] = color >> 8;
        pixels[x * 2 + 1] = color & 255;
    }
}
