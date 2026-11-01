#ifndef LAMA_ROUTING_H
#define LAMA_ROUTING_H
#include <stdint.h>
#define ROUTING_INPUTS 8

typedef enum
{
    TARGET_OFF,
    TARGET_PITCH,
    TARGET_VOWEL,
    TARGET_VOICE,
    TARGET_VIBRATO,
    TARGET_VIBRATO_RATE,
    TARGET_GLIDE,
    TARGET_ASPIRATION,
    TARGET_LEVEL,
    TARGET_ATTACK,
    TARGET_DECAY,
    TARGET_SUSTAIN,
    TARGET_RELEASE,
    TARGET_COUNT
} RoutingTarget;

void routing_init(void);
const char *routing_input_name(unsigned input);
const char *routing_target_name(RoutingTarget target);
RoutingTarget routing_get_target(unsigned input);
void routing_set_target(unsigned input, RoutingTarget target);
/* Core 0: MCP3208 channel 0..7, raw 0..4095. Last changed source wins.
 * At most one message per input per 20 ms, with 16-code hysteresis. */
void routing_update(unsigned input, uint16_t value, uint64_t now_us);
#endif
