#include "controls/routing.h"
#include "messaging/messenger.h"
#include "controls/calibration.h"
#include <string.h>

static struct
{
    RoutingTarget target;
    uint16_t sent_value;
    uint64_t sent_at;
    uint32_t calibration_revision;
    bool sent;
} inputs[ROUTING_INPUTS];

static const char *names[TARGET_COUNT] = {
    "OFF",
    "PITCH",
    "VOWEL",
    "VOICE",
    "VIBRATO",
    "VIB RATE",
    "GLIDE",
    "ASPIRATION",
    "LEVEL",
    "ATTACK",
    "DECAY",
    "SUSTAIN",
    "RELEASE",
};
static const MonkEventType events[TARGET_COUNT] = {
    MONK_SET_PITCH,
    MONK_SET_PITCH,
    MONK_SET_VOWEL,
    MONK_SET_VOICE,
    MONK_SET_VIBRATO,
    MONK_SET_VIBRATO_RATE,
    MONK_SET_GLIDE,
    MONK_SET_ASPIRATION,
    MONK_SET_LEVEL,
    MONK_SET_ATTACK,
    MONK_SET_DECAY,
    MONK_SET_SUSTAIN,
    MONK_SET_RELEASE,
};

void routing_init(void)
{
    memset(inputs, 0, sizeof(inputs));
}

const char *routing_input_name(unsigned input)
{
    static const char *labels[] = {
        "POT 1",
        "POT 2",
        "POT 3",
        "POT 4",
        "CV 1",
        "CV 2",
        "CV 3",
        "CV 4",
    };
    return input < ROUTING_INPUTS ? labels[input] : "?";
}

const char *routing_target_name(RoutingTarget target)
{
    return (unsigned)target < TARGET_COUNT ? names[target] : "?";
}

RoutingTarget routing_get_target(unsigned input)
{
    return input < ROUTING_INPUTS ? inputs[input].target : TARGET_OFF;
}

void routing_set_target(unsigned input, RoutingTarget target)
{
    if (input >= ROUTING_INPUTS || (unsigned)target >= TARGET_COUNT)
    {
        return;
    }

    if (inputs[input].target != target)
    {
        inputs[input].target = target;
        inputs[input].sent = false;
    }
}

void routing_update(unsigned input, uint16_t value, uint64_t now_us)
{
    if (input >= ROUTING_INPUTS || inputs[input].target == TARGET_OFF)
    {
        return;
    }

    if (value > 4095)
    {
        value = 4095;
    }

    int difference = (int)value - inputs[input].sent_value;
    uint32_t revision = calibration_revision(CAL_MCP3208);
    if (inputs[input].sent && inputs[input].calibration_revision == revision &&
        (now_us - inputs[input].sent_at < 20000 || (difference > -16 && difference < 16)))
    {
        return;
    }

    float normalized = calibration_normalized(CAL_MCP3208, value);
    float mapped = normalized;
    RoutingTarget target = inputs[input].target;
    switch (target)
    {
    case TARGET_PITCH:
        mapped = monk_note_to_hz(36.0f + normalized * 48.0f);
        break;
    case TARGET_ATTACK:
    case TARGET_DECAY:
        mapped *= 2.0f;
        break;
    case TARGET_RELEASE:
        mapped *= 4.0f;
        break;
    default:
        break;
    }

    if (add_message(events[target], mapped))
    {
        inputs[input].calibration_revision = revision;
        inputs[input].sent_value = value;
        inputs[input].sent_at = now_us;
        inputs[input].sent = true;
    }
}
