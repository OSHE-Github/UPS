/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UPS_OLED_H
#define UPS_OLED_H
#include "stm32l1xx_hal.h"

/* Adafruit 4440: SSD1306, 128 x 32, 7-bit address 0x3c.
 * Uses the caller's initialized I2C handle. No board-specific GPIO here.
 */
HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *bus);
HAL_StatusTypeDef OLED_TestFrame(I2C_HandleTypeDef *bus, uint32_t frame);
HAL_StatusTypeDef OLED_Text(I2C_HandleTypeDef *bus, const char *a, const char *b,
                            const char *c, const char *d);
#endif
