#ifndef TANTRICYPHONS_CONSOLE_COMMAND_H
#define TANTRICYPHONS_CONSOLE_COMMAND_H
#include "messaging/messenger.h"
#include "controls/routing.h"
#include <stdbool.h>
#define CONSOLE_LINE_SIZE 96

typedef enum
{
    CONSOLE_NONE,
    CONSOLE_HELP,
    CONSOLE_INFO,
    CONSOLE_STATUS,
    CONSOLE_RESET_STATS,
    CONSOLE_CONNECTIONS,
    CONSOLE_ADC,
    CONSOLE_ROUTES,
    CONSOLE_CAL,
    CONSOLE_EVENT,
    CONSOLE_ASSIGN,
    CONSOLE_SAVE,
} ConsoleAction;

typedef struct
{
    ConsoleAction action;
    MonkEventType event;
    float value;
    unsigned input;
    RoutingTarget target;
} ConsoleCommand;

/* No allocation or hardware access. Returns a static error string on failure. */
bool console_parse(const char *line, ConsoleCommand *command, const char **error);
#endif
