#ifndef BQ25730_H
#define BQ25730_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"

/* Driver return codes distinguish bus errors from configuration verification failures. */
typedef enum {
  BQ25730_OK = 0,
  BQ25730_ERROR_I2C,
  BQ25730_ERROR_VERIFY,
  BQ25730_ERROR_INVALID_ARG
} bq25730_result_t;

/* Minimal device handle holding the bus instance and I2C address. */
typedef struct {
  I2C_HandleTypeDef *i2c;
  uint8_t address_7bit;
} bq25730_t;

/* Snapshot of the charger state used by the supervisor and debugger. */
typedef struct {
  uint8_t manufacturer_id;
  uint8_t device_id;
  uint16_t charge_option0;
  uint16_t charge_option1;
  uint16_t charge_current;
  uint16_t charge_voltage;
  uint16_t input_voltage;
  uint16_t iin_host;
  uint16_t charger_status;
  uint16_t prochot_status;
  bool device_id_valid;
  bool fault_present;
} bq25730_snapshot_t;

/* Initialize the driver handle before probe or configuration calls. */
void bq25730_init(bq25730_t *dev, I2C_HandleTypeDef *i2c, uint8_t address_7bit);
/* Check that the expected BQ device is present on the configured address. */
bq25730_result_t bq25730_probe(const bq25730_t *dev, bq25730_snapshot_t *snapshot);
/* Read the register set the firmware uses for validation and runtime decisions. */
bq25730_result_t bq25730_read_snapshot(const bq25730_t *dev, bq25730_snapshot_t *snapshot);
/* Apply the current phase-1 charging policy and verify each register write. */
bq25730_result_t bq25730_configure_phase1(const bq25730_t *dev,
                                          const board_config_t *cfg,
                                          bq25730_snapshot_t *snapshot);
/* Reduce the full snapshot down to the fault condition the supervisor cares about. */
bool bq25730_snapshot_has_fault(const bq25730_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
