#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* All board-specific wiring and phase-1 tuning values live in one struct for portability. */
typedef struct {
  I2C_HandleTypeDef *i2c;
  uint8_t bq25730_address_7bit;
  uint8_t mpq5031_address_7bit;
  uint32_t startup_delay_ms;
  uint32_t run_poll_interval_ms;
  uint32_t fault_poll_interval_ms;
  uint16_t bq_charge_voltage_mv;
  uint16_t bq_charge_current_ma;
  uint16_t bq_input_current_limit_ma;
  uint16_t bq_input_voltage_limit_mv;
  uint16_t bq_rac_milliohm;
  uint16_t bq_rsr_milliohm;
  uint16_t mpq_pdo1_current_ma;
  GPIO_TypeDef *led_port;
  uint16_t led_pin;
  GPIO_TypeDef *prochot_port;
  uint16_t prochot_pin;
  GPIO_TypeDef *chrg_ok_port;
  uint16_t chrg_ok_pin;
  GPIO_TypeDef *vsel1_port;
  uint16_t vsel1_pin;
  GPIO_TypeDef *vsel2_port;
  uint16_t vsel2_pin;
} board_config_t;

/* Build the currently supported phase-1 hardware configuration. */
board_config_t board_config_make_phase1(I2C_HandleTypeDef *i2c);

#ifdef __cplusplus
}
#endif

#endif
