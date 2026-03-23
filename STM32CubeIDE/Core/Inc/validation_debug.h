#ifndef VALIDATION_DEBUG_H
#define VALIDATION_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_supervisor.h"
#include "board_config.h"

/* Expected BQ register values and per-field match flags exposed to debugger watches. */
typedef struct {
  uint16_t option0_mask;
  uint16_t option0_value;
  uint16_t option1_mask;
  uint16_t option1_value;
  uint16_t charge_current;
  uint16_t charge_voltage;
  uint16_t input_voltage;
  uint16_t iin_host;
  bool option0_matches;
  bool option1_matches;
  bool charge_current_matches;
  bool charge_voltage_matches;
  bool input_voltage_matches;
  bool iin_host_matches;
} validation_bq_expectation_t;

/* Expected MPQ register masks and match flags exposed to debugger watches. */
typedef struct {
  uint16_t pdo_type_mask;
  uint16_t pdo_type_value;
  uint16_t pdo_i1_mask;
  uint16_t pdo_i1_value;
  uint16_t ctl1_mask;
  uint16_t ctl1_value;
  uint16_t ctl2_mask;
  uint16_t ctl2_value;
  uint16_t ctl3_mask;
  uint16_t ctl3_value;
  uint16_t ctl4_mask;
  uint16_t ctl4_value;
  bool pdo_type_matches;
  bool pdo_i1_matches;
  bool ctl1_matches;
  bool ctl2_matches;
  bool ctl3_matches;
  bool ctl4_matches;
} validation_mpq_expectation_t;

/* One debugger-friendly snapshot of current firmware state plus validation metadata. */
typedef struct {
  uint32_t magic;
  uint32_t capture_count;
  uint32_t timestamp_ms;
  bool initialized;
  bool error_handler_entered;
  const char *state_name;
  const char *fault_name;
  app_supervisor_status_t status;
  board_config_t config;
  validation_bq_expectation_t bq_expected;
  validation_mpq_expectation_t mpq_expected;
  bool nominal_run_reached;
  bool bq_config_matches;
  bool mpq_config_matches;
  bool gpio_vsel_matches;
} validation_debug_snapshot_t;

extern volatile validation_debug_snapshot_t g_validation_debug;

void validation_debug_init(const board_config_t *cfg);
void validation_debug_capture(uint32_t now_ms);
void validation_debug_note_error_handler(void);

#ifdef __cplusplus
}
#endif

#endif
