#include "console_command.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *name;
    MonkEventType event;
    RoutingTarget target;
    float maximum;
} Parameter;

static const Parameter parameters[] = {
    {"vowel", MONK_SET_VOWEL, TARGET_VOWEL, 1},
    {"voice", MONK_SET_VOICE, TARGET_VOICE, 1},
    {"vibrato", MONK_SET_VIBRATO, TARGET_VIBRATO, 1},
    {"vib_rate", MONK_SET_VIBRATO_RATE, TARGET_VIBRATO_RATE, 1},
    {"glide", MONK_SET_GLIDE, TARGET_GLIDE, 1},
    {"aspiration", MONK_SET_ASPIRATION, TARGET_ASPIRATION, 1},
    {"level", MONK_SET_LEVEL, TARGET_LEVEL, 1},
    {"attack", MONK_SET_ATTACK, TARGET_ATTACK, 2},
    {"decay", MONK_SET_DECAY, TARGET_DECAY, 2},
    {"sustain", MONK_SET_SUSTAIN, TARGET_SUSTAIN, 1},
    {"release", MONK_SET_RELEASE, TARGET_RELEASE, 4},
};

static const Parameter *find_parameter(const char *name)
{
    for (unsigned i = 0; i < sizeof(parameters) / sizeof(parameters[0]); ++i)
    {
        if (!strcmp(name, parameters[i].name))
        {
            return &parameters[i];
        }
    }

    return NULL;
}

static bool integer(const char *text, long maximum, long *value)
{
    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (errno || !*text || *end || number < 0 || number > maximum)
    {
        return false;
    }

    *value = number;
    return true;
}

bool console_parse(const char *line, ConsoleCommand *command, const char **error)
{
    *command = (ConsoleCommand){0};
    *error = "Unknown command; type help";
    size_t length = strlen(line);
    if (length >= CONSOLE_LINE_SIZE)
    {
        *error = "Line too long";
        return false;
    }

    char copy[CONSOLE_LINE_SIZE];
    memcpy(copy, line, length + 1);
    char *tokens[4];
    unsigned count = 0;
    char *cursor = copy;
    while (*cursor)
    {
        while (isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

        if (!*cursor)
        {
            break;
        }

        if (count == 4)
        {
            *error = "Too many arguments";
            return false;
        }

        tokens[count++] = cursor;
        while (*cursor && !isspace((unsigned char)*cursor))
        {
            ++cursor;
        }

        if (*cursor)
        {
            *cursor++ = '\0';
        }
    }

    if (!count)
    {
        return true;
    }

    static const struct
    {
        const char *name;
        ConsoleAction action;
    } simple[] = {
        {"help", CONSOLE_HELP},
        {"info", CONSOLE_INFO},
        {"status", CONSOLE_STATUS},
        {"resetstats", CONSOLE_RESET_STATS},
        {"connections", CONSOLE_CONNECTIONS},
        {"adc", CONSOLE_ADC},
        {"routes", CONSOLE_ROUTES},
        {"cal", CONSOLE_CAL},
        {"save", CONSOLE_SAVE},
    };

    for (unsigned i = 0; i < sizeof(simple) / sizeof(simple[0]); ++i)
    {
        if (!strcmp(tokens[0], simple[i].name))
        {
            if (count != 1)
            {
                *error = "Command takes no arguments";
                return false;
            }

            command->action = simple[i].action;
            return true;
        }
    }

    if (!strcmp(tokens[0], "set"))
    {
        *error = "Usage: set PARAM VALUE (0..1; attack/decay 0..2s; release 0..4s)";
        if (count != 3)
        {
            return false;
        }

        const Parameter *parameter = find_parameter(tokens[1]);
        if (!parameter)
        {
            *error = "Unknown parameter; type help";
            return false;
        }

        char *end;
        errno = 0;
        float value = strtof(tokens[2], &end);
        if (errno || !*tokens[2] || *end || !isfinite(value) || value < 0 ||
            value > parameter->maximum)
        {
            return false;
        }

        command->action = CONSOLE_EVENT;
        command->event = parameter->event;
        command->value = value;
        return true;
    }

    if (!strcmp(tokens[0], "note"))
    {
        *error = "Usage: note NOTE [VELOCITY], both 0..127";
        long note, velocity = 100;
        if ((count != 2 && count != 3) || !integer(tokens[1], 127, &note) ||
            (count == 3 && !integer(tokens[2], 127, &velocity)))
        {
            return false;
        }

        command->action = CONSOLE_EVENT;
        command->event = velocity ? MONK_NOTE_ON : MONK_NOTE_OFF;
        command->value = velocity ? note * 128 + velocity : note;
        return true;
    }

    if (!strcmp(tokens[0], "off"))
    {
        *error = "Usage: off [NOTE], note 0..127";
        long note = 0;
        if ((count != 1 && count != 2) || (count == 2 && !integer(tokens[1], 127, &note)))
        {
            return false;
        }

        command->action = CONSOLE_EVENT;
        command->event = count == 2 ? MONK_NOTE_OFF : MONK_ALL_NOTES_OFF;
        command->value = note;
        return true;
    }

    if (!strcmp(tokens[0], "assign"))
    {
        *error = "Usage: assign INPUT TARGET, input 0..7; target off/pitch/parameter";
        long input;
        if (count != 3 || !integer(tokens[1], 7, &input))
        {
            return false;
        }

        RoutingTarget target;
        const Parameter *parameter = find_parameter(tokens[2]);
        if (!strcmp(tokens[2], "off"))
        {
            target = TARGET_OFF;
        }
        else if (!strcmp(tokens[2], "pitch"))
        {
            target = TARGET_PITCH;
        }
        else if (parameter)
        {
            target = parameter->target;
        }
        else
        {
            return false;
        }

        command->action = CONSOLE_ASSIGN;
        command->input = (unsigned)input;
        command->target = target;
        return true;
    }

    return false;
}
