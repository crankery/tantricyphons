#include "diagnostics/console_command.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ConsoleCommand parse(const char *line)
{
    ConsoleCommand command;
    const char *error;
    assert(console_parse(line, &command, &error));
    return command;
}

static void reject(const char *line)
{
    ConsoleCommand command;
    const char *error;
    assert(!console_parse(line, &command, &error));
    assert(error && *error);
    assert(command.action == CONSOLE_NONE);
}

int main(void)
{
    assert(parse("").action == CONSOLE_NONE);
    assert(parse("  \t  ").action == CONSOLE_NONE);
    assert(parse("help").action == CONSOLE_HELP);
    assert(parse("info").action == CONSOLE_INFO);
    assert(parse("status").action == CONSOLE_STATUS);
    assert(parse("resetstats").action == CONSOLE_RESET_STATS);
    assert(parse("connections").action == CONSOLE_CONNECTIONS);
    assert(parse("adc").action == CONSOLE_ADC);
    assert(parse("routes").action == CONSOLE_ROUTES);
    assert(parse("cal").action == CONSOLE_CAL);
    assert(parse("save").action == CONSOLE_SAVE);
    ConsoleCommand command = parse("note 69");
    assert(command.action == CONSOLE_EVENT && command.event == MONK_NOTE_ON);
    assert(command.value == 69 * 128 + 100);
    command = parse("note 127 127");
    assert(command.value == 127 * 128 + 127);
    command = parse("note 69 0");
    assert(command.event == MONK_NOTE_OFF && command.value == 69);
    assert(parse("off").event == MONK_ALL_NOTES_OFF);
    command = parse("off 0");
    assert(command.event == MONK_NOTE_OFF && command.value == 0);
    command = parse("  set\tvowel  0.7  ");
    assert(command.event == MONK_SET_VOWEL && fabsf(command.value - 0.7f) < 0.0001f);
    assert(parse("set attack 2").value == 2);
    assert(parse("set release 4").value == 4);
    assert(parse("set vib_rate 0").event == MONK_SET_VIBRATO_RATE);
    command = parse("assign 0 vibrato");
    assert(command.action == CONSOLE_ASSIGN && command.input == 0 &&
           command.target == TARGET_VIBRATO);
    assert(parse("assign 7 pitch").target == TARGET_PITCH);
    assert(parse("assign 4 off").target == TARGET_OFF);

    const char *invalid[] = {
        "unknown",
        "help extra",
        "note",
        "note -1",
        "note 128",
        "note 69 128",
        "note 69 -1",
        "note 69 1 extra",
        "note 69.0",
        "off -1",
        "off 128",
        "off 69 extra",
        "set",
        "set vowel",
        "set pitch 440",
        "set vowel -0.1",
        "set vowel 1.1",
        "set vowel nan",
        "set vowel inf",
        "set vowel 1e100",
        "set vowel .",
        "set vowel 0.5junk",
        "set vowel 0.5 extra",
        "set decay 2.1",
        "set release 4.1",
        "assign",
        "assign 8 vowel",
        "assign -1 vowel",
        "assign 0 banana",
        "connections extra",
        "resetstats extra",
        "save now",
        "a b c d e",
        "note 99999999999999999999999999999999999",
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        reject(invalid[i]);
    }

    char oversized[CONSOLE_LINE_SIZE + 1];
    memset(oversized, 'x', sizeof(oversized));
    oversized[sizeof(oversized) - 1] = '\0';
    reject(oversized);
    // Exercise arbitrary bounded input under ASan/UBSan, including whitespace.
    uint32_t seed = 1;
    for (unsigned i = 0; i < 10000; ++i)
    {
        char line[CONSOLE_LINE_SIZE];
        unsigned size = i % CONSOLE_LINE_SIZE;
        for (unsigned j = 0; j < size; ++j)
        {
            seed = seed * 1664525u + 1013904223u;
            line[j] = (char)(1 + seed % 127);
        }

        line[size] = '\0';
        const char *error;
        console_parse(line, &command, &error);
    }

    puts("USB console command parsing, validation, bounds, and fuzz tests passed");
}
