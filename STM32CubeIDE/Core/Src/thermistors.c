/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thermistors.h"
#include <math.h>

/* Each divider: 3V3--10k--ADC--10k NTC(B3950)--GND.
 * 100nF from each ADC node to ground.
 * These are measurements only; charging policy is not implemented here.
 */
static ADC_HandleTypeDef adc;
static uint8_t ready;
volatile ThermistorReading thermistors[THERMISTOR_COUNT];
volatile uint32_t thermistor_errors;
volatile uint32_t thermistor_sample_tick;
volatile uint8_t thermistor_scan_complete;
#ifdef UPS_THERMISTORS
static const uint32_t channels[THERMISTOR_COUNT] = {
    ADC_CHANNEL_10, ADC_CHANNEL_11
};
#else
/* Battery 1/J2/PB13, 2/J5/PB14, 3/J11/PC0, 4/J6/PB15, board/J10/PC1. */
static const uint32_t channels[THERMISTOR_COUNT] = {
    ADC_CHANNEL_19, ADC_CHANNEL_20, ADC_CHANNEL_10, ADC_CHANNEL_21,
    ADC_CHANNEL_11
};
#endif

ThermistorReading Thermistors_Convert(uint16_t raw)
{
    ThermistorReading r = {raw, 0, THERM_OK};
    if (raw <= 4U) { r.status = THERM_SHORT; return r; }
    if (raw >= 4091U) { r.status = THERM_OPEN; return r; }
    const float resistance = 10000.0f * raw / (4095.0f - raw);
    const float celsius = 1.0f / (1.0f / 298.15f +
                                  logf(resistance / 10000.0f) / 3950.0f) - 273.15f;
    r.milli_celsius = (int32_t)(celsius * 1000.0f);
    if (celsius < -30.0f || celsius > 125.0f) r.status = THERM_OUT_OF_RANGE;
    return r;
}

HAL_StatusTypeDef Thermistors_Init(void)
{
    ready = 0;
    thermistor_scan_complete = 0;
    for (unsigned sensor = 0; sensor < THERMISTOR_COUNT; ++sensor) {
        thermistors[sensor] = (ThermistorReading){0, 0, THERM_ADC_ERROR};
    }
    /* ADC has an asynchronous HSI clock, even when the UPS CPU runs from MSI. */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    HAL_StatusTypeDef result = HAL_RCC_OscConfig(&osc);
    if (result != HAL_OK) return result;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);
#ifndef UPS_THERMISTORS
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &gpio);
#endif
    adc.Instance = ADC1;
    adc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
    adc.Init.Resolution = ADC_RESOLUTION_12B;
    adc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc.Init.ScanConvMode = ADC_SCAN_DISABLE;
    adc.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    adc.Init.LowPowerAutoWait = ADC_AUTOWAIT_DISABLE;
    adc.Init.LowPowerAutoPowerOff = ADC_AUTOPOWEROFF_DISABLE;
    adc.Init.ChannelsBank = ADC_CHANNELS_BANK_A;
    adc.Init.ContinuousConvMode = DISABLE;
    adc.Init.NbrOfConversion = 1;
    adc.Init.DiscontinuousConvMode = DISABLE;
    adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    result = HAL_ADC_Init(&adc);
    ready = result == HAL_OK;
    HAL_Delay(10); // Divider RC settling before the first sample.
    return result;
}

void Thermistors_Poll(void)
{
    thermistor_scan_complete = 0;
    for (unsigned sensor = 0; sensor < THERMISTOR_COUNT; ++sensor) {
        ThermistorReading reading = {0, 0, THERM_ADC_ERROR};
        ADC_ChannelConfTypeDef config = {0};
        config.Channel = channels[sensor];
        config.Rank = ADC_REGULAR_RANK_1;
        config.SamplingTime = ADC_SAMPLETIME_384CYCLES;
        HAL_StatusTypeDef result = ready ? HAL_ADC_ConfigChannel(&adc, &config) : HAL_ERROR;
        uint32_t sum = 0;
        /* Discard first conversion after changing channel, then average eight. */
        for (unsigned sample = 0; sample < 9 && result == HAL_OK; ++sample) {
            result = HAL_ADC_Start(&adc);
            if (result == HAL_OK) result = HAL_ADC_PollForConversion(&adc, 20);
            if (result == HAL_OK) {
                const uint16_t raw = (uint16_t)HAL_ADC_GetValue(&adc);
                if (sample) sum += raw;
            }
            HAL_StatusTypeDef stopped = HAL_ADC_Stop(&adc);
            if (result == HAL_OK) result = stopped;
        }
        if (result == HAL_OK) reading = Thermistors_Convert((uint16_t)((sum + 4U) / 8U));
        else ++thermistor_errors;
        thermistors[sensor] = reading; // Never retain a stale valid temperature on failure.
    }
    thermistor_sample_tick = HAL_GetTick();
    thermistor_scan_complete = 1;
}
