#ifndef LAMA_SETTINGS_H
#define LAMA_SETTINGS_H

#include <stdbool.h>

typedef enum
{
    SETTINGS_IDLE,
    SETTINGS_PENDING,
    SETTINGS_SAVED,
    SETTINGS_ERROR
} SettingsStatus;

/* Core 0; load after menu_init and before launching core 1. */
void settings_load(void);
void settings_request_save(void);
void settings_poll(bool display_ready);
SettingsStatus settings_status(void);
bool settings_dirty(void);
void settings_restore_defaults(void);

#endif
