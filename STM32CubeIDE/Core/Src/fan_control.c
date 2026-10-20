/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fan_control.h"
#include "thermistors.h"

volatile uint8_t fan_is_on;

static void set_fan(uint8_t on)
{
    fan_is_on = on;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Fan_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* Request cooling until all sensors have supplied a valid sample. */
    set_fan(1);
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void Fan_Update(void)
{
    if (!thermistor_scan_complete ||
        (uint32_t)(HAL_GetTick() - thermistor_sample_tick) > FAN_SAMPLE_MAX_AGE_MS) {
        set_fan(1);
        return;
    }
    int32_t hottest = INT32_MIN;
    for (unsigned sensor = 0; sensor < THERMISTOR_COUNT; ++sensor) {
        const ThermistorReading reading = thermistors[sensor];
        if (reading.status != THERM_OK) {
            set_fan(1);
            return;
        }
        if (reading.milli_celsius > hottest) hottest = reading.milli_celsius;
    }
    if (hottest >= FAN_ON_MILLI_C) set_fan(1);
    else if (hottest <= FAN_OFF_MILLI_C) set_fan(0);
    /* Between thresholds, retain the previous state to avoid chatter. */
}
