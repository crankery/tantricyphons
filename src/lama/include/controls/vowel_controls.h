#ifndef LAMA_VOWEL_CONTROLS_H
#define LAMA_VOWEL_CONTROLS_H
#include <stdbool.h>
#include <stdint.h>

#define VOWEL_CV_GAIN 0.6f
#define VOWEL_CV_FULL_SCALE_VOLTS 5.0f
#define VOWEL_CV_DEPTH 0.5f
#define VOWEL_UPDATE_INTERVAL_US 5000u
#define VOWEL_CHANGE_THRESHOLD (4.0f / 4095.0f)

typedef struct
{
    uint16_t pot, cv;
    unsigned seen;
    bool sent;
    float last_value;
    uint64_t sent_at;
    uint32_t calibration_revision;
} VowelControls;

/* Both readings use the shared Pico ADC calibration. Center pot = zero
 * bipolar offset; 0 V CV = zero modulation; final voice value is 0..1. */
float vowel_controls_value(uint16_t pot, uint16_t cv);
void vowel_controls_init(VowelControls *controls);
/* Physical Pico ADC1 = CV, ADC2 = pot. Other channels are ignored. */
void vowel_controls_sample(VowelControls *controls, unsigned channel, uint16_t raw);
/* Core 0: emits ADC-source MONK_SET_VOWEL only when meaningfully changed. */
void vowel_controls_poll(VowelControls *controls, uint64_t now_us);
#endif
