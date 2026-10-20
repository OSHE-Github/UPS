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
extern volatile ThermistorReading thermistors[2];
extern volatile uint32_t thermistor_errors;
HAL_StatusTypeDef Thermistors_Init(void);
void Thermistors_Poll(void);
/* Conversion kept separate for resistor-substitution tests. */
ThermistorReading Thermistors_Convert(uint16_t raw);
#endif
