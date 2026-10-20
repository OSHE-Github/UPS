/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "battery_voltage.h"

#define BQ_ADDRESS (0x6BU << 1)
#define IO_TIMEOUT_MS 20U
#define SAMPLE_PERIOD_MS 1000U
#define CONVERSION_TIMEOUT_MS 500U
#define MAX_SAMPLE_AGE_MS 2000U
#define REG_CHARGE_OPTION0_HI 0x01U
#define REG_VBAT 0x2CU
#define REG_MANUFACTURER 0x2EU
#define REG_DEVICE 0x2FU
#define REG_ADC_LO 0x3AU
#define REG_ADC_HI 0x3BU

volatile BatteryVoltageReading battery_voltage;
volatile uint32_t battery_voltage_errors;
static I2C_HandleTypeDef *i2c;
static enum { IDLE, WAKING, CONVERTING } phase;
static uint32_t phase_tick, attempt_tick;

static HAL_StatusTypeDef read_byte(uint8_t reg, uint8_t *value)
{
    return HAL_I2C_Mem_Read(i2c, BQ_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
                            value, 1, IO_TIMEOUT_MS);
}

static HAL_StatusTypeDef write_byte(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(i2c, BQ_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
                             &value, 1, IO_TIMEOUT_MS);
}

static void fail(BatteryVoltageStatus status)
{
    battery_voltage = (BatteryVoltageReading){0, 0, 0, status};
    ++battery_voltage_errors;
    phase = IDLE;
    attempt_tick = HAL_GetTick(); /* Bound retries when the bus/device is absent. */
}

void BatteryVoltage_Init(I2C_HandleTypeDef *bus)
{
    i2c = bus;
    phase = IDLE;
    attempt_tick = HAL_GetTick() - SAMPLE_PERIOD_MS;
    battery_voltage_errors = 0;
    battery_voltage = (BatteryVoltageReading){0, 0, 0, BATTERY_VOLTAGE_NOT_READY};
}

BatteryVoltageReading BatteryVoltage_Get(void)
{
    BatteryVoltageReading r = battery_voltage;
    if ((r.status == BATTERY_VOLTAGE_OK ||
         r.status == BATTERY_VOLTAGE_AT_RANGE_LIMIT) &&
        (uint32_t)(HAL_GetTick() - r.sample_tick) > MAX_SAMPLE_AGE_MS) {
        r.status = BATTERY_VOLTAGE_STALE;
        r.millivolts = 0;
    }
    return r;
}

void BatteryVoltage_Poll(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t option, lo, hi, manufacturer, device, raw;
    if (!i2c) return;

    if (phase == IDLE) {
        if ((uint32_t)(now - attempt_tick) < SAMPLE_PERIOD_MS) return;
        attempt_tick = now;
        /* Identify the charger before writing any configuration. */
        if (read_byte(REG_MANUFACTURER, &manufacturer) != HAL_OK ||
            read_byte(REG_DEVICE, &device) != HAL_OK) {
            fail(BATTERY_VOLTAGE_I2C_ERROR); return;
        }
        if (manufacturer != 0x40U || device != 0xD5U) {
            fail(BATTERY_VOLTAGE_WRONG_DEVICE); return;
        }
        if (read_byte(REG_CHARGE_OPTION0_HI, &option) != HAL_OK ||
            ((option & 0x80U) &&
             write_byte(REG_CHARGE_OPTION0_HI, option & 0x7FU) != HAL_OK)) {
            fail(BATTERY_VOLTAGE_I2C_ERROR); return;
        }
        phase = WAKING;
        phase_tick = HAL_GetTick();
        return;
    }
    if (phase == WAKING) {
        /* Allow the analog supply to settle after leaving low-power mode. */
        if ((uint32_t)(now - phase_tick) < 25U) return;
        if (read_byte(REG_ADC_LO, &lo) != HAL_OK ||
            read_byte(REG_ADC_HI, &hi) != HAL_OK ||
            write_byte(REG_ADC_LO, lo | 0x01U) != HAL_OK ||
            write_byte(REG_ADC_HI, (hi & 0x3FU) | 0x40U) != HAL_OK) {
            fail(BATTERY_VOLTAGE_I2C_ERROR); return;
        }
        /* One-shot conversion: retain other channel enables/reserved bits.
         * Up to eight enabled channels require up to 200 ms per TI Table 8-38.
         */
        phase = CONVERTING;
        phase_tick = HAL_GetTick();
        return;
    }
    if ((uint32_t)(now - phase_tick) > CONVERSION_TIMEOUT_MS) {
        fail(BATTERY_VOLTAGE_TIMEOUT); return;
    }
    if (read_byte(REG_CHARGE_OPTION0_HI, &option) != HAL_OK ||
        read_byte(REG_ADC_LO, &lo) != HAL_OK ||
        read_byte(REG_ADC_HI, &hi) != HAL_OK) {
        fail(BATTERY_VOLTAGE_I2C_ERROR); return;
    }
    /* A reset or another ADC owner must not turn a cached register into a
     * seemingly fresh measurement. Check mode and enable bits again.
     */
    if ((option & 0x80U) || !(lo & 0x01U) || (hi & 0x80U)) {
        fail(BATTERY_VOLTAGE_CONFIG_ERROR); return;
    }
    if (hi & 0x40U) return; /* ADC_START clears only after one-shot completion. */
    if (read_byte(REG_VBAT, &raw) != HAL_OK) {
        fail(BATTERY_VOLTAGE_I2C_ERROR); return;
    }
    battery_voltage = (BatteryVoltageReading){
        (uint16_t)(2880U + 64U * raw), raw, HAL_GetTick(),
        (raw == 0U || raw == 255U) ? BATTERY_VOLTAGE_AT_RANGE_LIMIT :
                                    BATTERY_VOLTAGE_OK
    };
    phase = IDLE;
}
