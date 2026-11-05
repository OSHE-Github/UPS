/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "power_mock.h"
#include "oled.h"
#include <stdio.h>

static I2C_HandleTypeDef power_bus;
volatile uint32_t power_polls, bq_errors, pd_errors;
volatile uint16_t bq_id, bq_status, bq_faults_seen;
volatile uint16_t pd_id, pd_status1, pd_status2, pd_events_seen;
volatile uint8_t bq_online, pd_online;
volatile uint8_t pd_mismatch_seen;

HAL_StatusTypeDef PowerMock_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
    power_bus.Instance = I2C1;
    power_bus.Init.ClockSpeed = 100000;
    power_bus.Init.DutyCycle = I2C_DUTYCYCLE_2;
    power_bus.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    power_bus.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    power_bus.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    power_bus.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    return HAL_I2C_Init(&power_bus);
}

static HAL_StatusTypeDef read_word(uint8_t address, uint8_t reg, uint16_t *value)
{
    uint8_t bytes[2];
    HAL_StatusTypeDef result = HAL_I2C_Mem_Read(&power_bus, address << 1, reg,
                                              I2C_MEMADD_SIZE_8BIT, bytes, 2, 20);
    if (result == HAL_OK)
        *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    return result;
}

void PowerMock_Poll(void)
{
    uint16_t id = 0, first = 0, second = 0;
    bq_online = read_word(0x6b, 0x2e, &id) == HAL_OK && id == 0xd540 &&
                read_word(0x6b, 0x20, &first) == HAL_OK;
    bq_id = id;
    bq_status = bq_online ? first : 0;
    if (bq_online) bq_faults_seen |= first & 0x00ffU;
    else ++bq_errors;
    id = first = 0;
    pd_online = read_word(0x28, 0x12, &id) == HAL_OK && (id & 0xf000U) == 0x8000 &&
                read_word(0x28, 0x10, &first) == HAL_OK &&
                read_word(0x28, 0x11, &second) == HAL_OK;
    pd_id = id;
    pd_status1 = pd_online ? first : 0;
    pd_status2 = pd_online ? second : 0;
    if (pd_online) {
        pd_events_seen |= first & 0x001fU;
        if (second & 0x2000U) pd_mismatch_seen = 1;
    }
    else ++pd_errors;
    ++power_polls;
}

HAL_StatusTypeDef PowerMock_Show(I2C_HandleTypeDef *display, uint32_t frame)
{
    char top[22], bq[22], pd[22], faults[22];
    (void)snprintf(top, sizeof top, "SIM TEST %06lu", (unsigned long)(frame % 1000000U));
    if (bq_online)
        (void)snprintf(bq, sizeof bq, "BQ %04X", (unsigned)bq_status);
    else (void)snprintf(bq, sizeof bq, "BQ OFFLINE");
    if (pd_online)
        (void)snprintf(pd, sizeof pd, "PD %04X %04X",
                       (unsigned)pd_status1, (unsigned)pd_status2);
    else (void)snprintf(pd, sizeof pd, "PD OFFLINE");
    (void)snprintf(faults, sizeof faults, "LATCH B%02X P%02X M%u",
                   (unsigned)(bq_faults_seen & 0xffU),
                   (unsigned)(pd_events_seen & 0x1fU),
                   pd_mismatch_seen ? 1U : 0U);
    return OLED_Text(display, top, bq, pd, faults);
}
