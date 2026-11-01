#ifndef LAMA_CALIBRATION_H
#define LAMA_CALIBRATION_H
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    CAL_PICO,
    CAL_MCP3208,
    CAL_BANKS
} CalibrationBank;

/* Raw counts captured with 1 V / 4 V applied at the bank's reference jack:
 * Pico FREQ CV: ADC = jack * 0.6; MCP CV1: ADC = 1.5 + jack * 0.3.
 * The fitted ADC correction is shared by every channel on that converter. */
typedef struct
{
    float raw_1v, raw_4v;
} AdcCalibration;

void calibration_defaults(void);
AdcCalibration calibration_nominal(CalibrationBank bank);
AdcCalibration calibration_get(CalibrationBank bank);
bool calibration_valid(CalibrationBank bank, AdcCalibration value);
bool calibration_set(CalibrationBank bank, AdcCalibration value);
uint32_t calibration_revision(CalibrationBank bank);
float calibration_adc_volts(CalibrationBank bank, uint16_t raw);
float calibration_normalized(CalibrationBank bank, uint16_t raw);
/* Dedicated frequency-jack voltage, including its 0.6 input gain. */
float calibration_volts(uint16_t raw);
void calibration_capture_begin(CalibrationBank bank);
void calibration_capture_cancel(void);
void calibration_sample(CalibrationBank bank, uint16_t raw);
bool calibration_capture_result(float *value, bool *success);
#endif
