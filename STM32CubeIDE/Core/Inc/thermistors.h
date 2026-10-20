/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UPS_THERMISTORS_H
#define UPS_THERMISTORS_H
#include "stm32l1xx_hal.h"
typedef enum {
    THERM_OK = 0, THERM_OPEN, THERM_SHORT, THERM_ADC_ERROR, THERM_OUT_OF_RANGE
} ThermistorStatus;
typedef struct {
    uint16_t raw;
    int32_t milli_celsius;
    ThermistorStatus status;
} ThermistorReading;
/* The existing Nucleo breadboard has two sensors. Production has four cells
 * plus a board sensor; never silently reuse the breadboard channel map. */
#ifdef UPS_THERMISTORS
#define THERMISTOR_COUNT 2U
#else
typedef enum {
    THERM_BATTERY_1, THERM_BATTERY_2, THERM_BATTERY_3, THERM_BATTERY_4,
    THERM_BOARD, THERMISTOR_COUNT
} ThermistorChannel;
#endif
extern volatile ThermistorReading thermistors[THERMISTOR_COUNT];
extern volatile uint32_t thermistor_errors;
extern volatile uint32_t thermistor_sample_tick;
extern volatile uint8_t thermistor_scan_complete;
HAL_StatusTypeDef Thermistors_Init(void);
void Thermistors_Poll(void);
/* Conversion kept separate for resistor-substitution tests. */
ThermistorReading Thermistors_Convert(uint16_t raw);
#endif
