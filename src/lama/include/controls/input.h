#ifndef LAMA_INPUT_H
#define LAMA_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define INPUT_PICO_CHANNELS 3
#define INPUT_EXTERNAL_CHANNELS 8
#define INPUT_SCAN_INTERVAL_US 1000
#define ADC_CS 7
#define ADC_CLK 8
#define ADC_DIN 9
#define ADC_DOUT 10

/* Channel indices are physical ADC channels, with no voice assignments.
 * Raw range is 0..4095; each converter has its own voltage reference.
 * A snapshot is published only after both converters finish a full scan. */
typedef struct
{
    uint16_t pico[INPUT_PICO_CHANNELS];         /* ADC0..2 = GP26..28 */
    uint16_t external[INPUT_EXTERNAL_CHANNELS]; /* MCP3208 CH0..7 */
    float pico_normalized[INPUT_PICO_CHANNELS]; /* calibrated 0..1 */
    float external_normalized[INPUT_EXTERNAL_CHANNELS];
    uint32_t sequence;
    uint64_t timestamp_us;
} InputSnapshot;

typedef enum
{
    INPUT_ADC_PICO,   /* channels 0..2, GP26..28 */
    INPUT_ADC_MCP3208 /* channels 0..7 */
} InputAdc;

/* Called once per channel after each valid full scan, even if unchanged.
 * Runs inside poll_controls() on core 0, outside the snapshot lock.
 * Keep it short and nonblocking. Values are raw 0..4095. */
typedef void (*InputUpdateHandler)(InputAdc adc, unsigned channel, uint16_t value);

/* Set on core 0; NULL disables notifications. Registration persists across
 * input_init(). No notifications are generated for discarded scans. */
void input_set_update_handler(InputUpdateHandler handler);

/* Initialize and poll on core 0. Poll is nonblocking; call every UI loop.
 * Snapshot reads are safe from either core, outside interrupt handlers. */
void input_init(void);
void poll_controls(void);
bool input_get_snapshot(InputSnapshot *snapshot);

#endif
