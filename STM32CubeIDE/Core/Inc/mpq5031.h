#ifndef MPQ5031_H
#define MPQ5031_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"

/* Driver return codes separate I2C transport failures from verify failures. */
typedef enum {
  MPQ5031_OK = 0,
  MPQ5031_ERROR_I2C,
  MPQ5031_ERROR_VERIFY,
  MPQ5031_ERROR_INVALID_ARG
} mpq5031_result_t;

/* Device handle also stores the first read-clear status sample for later inspection. */
typedef struct {
  I2C_HandleTypeDef *i2c;
  uint8_t address_7bit;
  bool first_read_clear_valid;
  uint16_t first_status1;
  uint16_t first_status2;
} mpq5031_t;

/* Register view used by the supervisor and debugger to validate source-controller behavior. */
typedef struct {
  uint16_t id;
  uint16_t pdo_type;
  uint16_t pdo_i1;
  uint16_t ctl1;
  uint16_t ctl2;
  uint16_t ctl3;
  uint16_t ctl4;
  uint16_t status1;
  uint16_t status2;
  uint16_t first_status1;
  uint16_t first_status2;
  bool first_status_valid;
} mpq5031_snapshot_t;

/* Initialize the driver handle and clear any preserved read-clear metadata. */
void mpq5031_init(mpq5031_t *dev, I2C_HandleTypeDef *i2c, uint8_t address_7bit);
/* Probe the MPQ device by checking the vendor ID encoding. */
mpq5031_result_t mpq5031_probe(const mpq5031_t *dev, mpq5031_snapshot_t *snapshot);
/* Capture pre-configuration status and control registers for validation. */
mpq5031_result_t mpq5031_read_baseline(mpq5031_t *dev, mpq5031_snapshot_t *snapshot);
/* Capture the runtime register set used during steady-state polling. */
mpq5031_result_t mpq5031_read_runtime_status(mpq5031_t *dev, mpq5031_snapshot_t *snapshot);
/* Apply the current phase-1 USB-C source policy and verify key register writes. */
mpq5031_result_t mpq5031_configure_phase1(mpq5031_t *dev,
                                          const board_config_t *cfg,
                                          mpq5031_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
