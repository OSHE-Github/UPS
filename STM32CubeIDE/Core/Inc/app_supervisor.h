#ifndef APP_SUPERVISOR_H
#define APP_SUPERVISOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "bq25730.h"
#include "mpq5031.h"

/* High-level supervisor states used during bring-up, steady-state operation, and fault hold. */
typedef enum {
  APP_BOOT = 0,
  APP_WAIT_POWER_STABLE,
  APP_PROBE_DEVICES,
  APP_READ_BASELINE,
  APP_CONFIGURE_BQ,
  APP_CONFIGURE_MPQ,
  APP_RUN,
  APP_FAULT_HOLD
} app_state_t;

/* Fault codes are split by source so debugger output points at the failing stage directly. */
typedef enum {
  APP_FAULT_NONE = 0,
  APP_FAULT_PROCHOT_ASSERTED,
  APP_FAULT_BQ_NOT_PRESENT,
  APP_FAULT_MPQ_NOT_PRESENT,
  APP_FAULT_BQ_CONFIG,
  APP_FAULT_MPQ_CONFIG,
  APP_FAULT_BQ_STATUS,
  APP_FAULT_I2C,
  APP_FAULT_CONFIG_VERIFY
} app_fault_code_t;

/* Aggregated runtime status consumed by debugger watches and validation tooling. */
typedef struct {
  app_state_t state;
  app_fault_code_t last_fault;
  bool bq_present;
  bool mpq_present;
  bool prochot_asserted;
  bool chrg_ok;
  bool vsel1_level;
  bool vsel2_level;
  uint32_t last_transition_ms;
  uint32_t last_successful_poll_ms;
  bq25730_snapshot_t bq;
  mpq5031_snapshot_t mpq;
} app_supervisor_status_t;

/* Bind the MCU board wiring and initialize the state machine context. */
void app_supervisor_init(const board_config_t *cfg);
/* Advance the state machine once using the current millisecond tick count. */
void app_supervisor_step(uint32_t now_ms);
/* Return the current status snapshot for validation and observability. */
const app_supervisor_status_t *app_supervisor_get_status(void);

#ifdef __cplusplus
}
#endif

#endif
