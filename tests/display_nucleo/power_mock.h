/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UPS_POWER_MOCK_H
#define UPS_POWER_MOCK_H
#include "stm32l1xx_hal.h"
HAL_StatusTypeDef PowerMock_Init(void);
void PowerMock_Poll(void);
HAL_StatusTypeDef PowerMock_Show(I2C_HandleTypeDef *display, uint32_t frame);
#endif
