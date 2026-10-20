/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UPS_BATTERY_VOLTAGE_H
#define UPS_BATTERY_VOLTAGE_H
#include "stm32l1xx_hal.h"

typedef enum {
    BATTERY_VOLTAGE_NOT_READY,
    BATTERY_VOLTAGE_OK,
    BATTERY_VOLTAGE_I2C_ERROR,
    BATTERY_VOLTAGE_WRONG_DEVICE,
    BATTERY_VOLTAGE_TIMEOUT,
    BATTERY_VOLTAGE_CONFIG_ERROR,
    BATTERY_VOLTAGE_AT_RANGE_LIMIT,
    BATTERY_VOLTAGE_STALE
} BatteryVoltageStatus;

typedef struct {
    uint16_t millivolts;
    uint8_t raw;
    uint32_t sample_tick;
    BatteryVoltageStatus status;
} BatteryVoltageReading;

extern volatile BatteryVoltageReading battery_voltage;
extern volatile uint32_t battery_voltage_errors;

/* This module owns the charger's ADC and keeps EN_LWPWR cleared.
 * Current board is 1S4P: VBAT offset is 2880 mV (valid for 1S through 4S).
 * It does not configure charge current, charge voltage, or protection policy.
 */
void BatteryVoltage_Init(I2C_HandleTypeDef *bus);
void BatteryVoltage_Poll(void);
BatteryVoltageReading BatteryVoltage_Get(void);
#endif
