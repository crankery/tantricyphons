#ifndef LAMA_MENU_H
#define LAMA_MENU_H
#include <stdbool.h>
#include <stdint.h>
#include "controls/routing.h"
#include "controls/calibration.h"
#include "settings/settings.h"
#define MENU_HEIGHT 96
#define MENU_ROW_BYTES 480

typedef enum
{
    MENU_MAIN,
    MENU_ASSIGNMENTS,
    MENU_CAL_BANK,
    MENU_CAL_1V,
    MENU_CAL_WAIT_1V,
    MENU_CAL_4V,
    MENU_CAL_WAIT_4V,
    MENU_CAL_REVIEW,
    MENU_CAL_FAILED,
    MENU_CAL_DONE,
    MENU_SAVE_CONFIRM,
    MENU_SAVE_RESULT,
    MENU_RESET_CONFIRM,
    MENU_RESET_DONE
} MenuPage;

typedef struct
{
    bool open, editing, dirty;
    unsigned selected;
    RoutingTarget draft;
    RoutingTarget assignments[ROUTING_INPUTS];
    uint32_t revision;
    MenuPage page;
    AdcCalibration calibration;
    CalibrationBank calibration_bank;
    SettingsStatus save_status;
} MenuView;

void menu_init(void);
void menu_turn(int steps);
void menu_press(void);
void menu_back(void);
void menu_poll(void);
MenuView menu_get_view(void);
void menu_render_row(const MenuView *view, unsigned row, uint8_t *pixels);
#endif
