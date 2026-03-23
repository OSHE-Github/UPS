#include "board_config.h"

board_config_t board_config_make_phase1(I2C_HandleTypeDef *i2c)
{
  /* Centralize the phase-1 board assumptions so validation and firmware use one baseline. */
  board_config_t cfg = {
      .i2c = i2c,
      .bq25730_address_7bit = 0x6B,
      .mpq5031_address_7bit = 0x28,
      .startup_delay_ms = 15U,
      .run_poll_interval_ms = 250U,
      .fault_poll_interval_ms = 1000U,
      .bq_charge_voltage_mv = 4200U,
      .bq_charge_current_ma = 1000U,
      .bq_input_current_limit_ma = 3000U,
      .bq_input_voltage_limit_mv = 4608U,
      .bq_rac_milliohm = 10U,
      .bq_rsr_milliohm = 10U,
      .mpq_pdo1_current_ma = 5000U,
      .led_port = LED_GPIO_Port,
      .led_pin = LED_Pin,
      .prochot_port = PROCHOT_N_GPIO_Port,
      .prochot_pin = PROCHOT_N_Pin,
      .chrg_ok_port = CHRG_OK_GPIO_Port,
      .chrg_ok_pin = CHRG_OK_Pin,
      .vsel1_port = VSEL1_GPIO_Port,
      .vsel1_pin = VSEL1_Pin,
      .vsel2_port = VSEL2_GPIO_Port,
      .vsel2_pin = VSEL2_Pin,
  };

  return cfg;
}
