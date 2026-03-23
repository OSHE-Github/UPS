#include "app_supervisor.h"

#include <string.h>

/* Single global supervisor context because the firmware only manages one UPS instance. */
typedef struct {
  const board_config_t *cfg;
  bq25730_t bq_dev;
  mpq5031_t mpq_dev;
  app_supervisor_status_t status;
  bool initialized;
  bool last_chrg_ok_sample;
  uint16_t last_mpq_status1;
  uint16_t last_mpq_status2;
} app_supervisor_context_t;

static app_supervisor_context_t g_supervisor;

/* PROCHOT_N is wired active-low, so a low GPIO sample means the fault is asserted. */
static bool app_gpio_is_active_low(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

/* Other status pins are consumed as conventional active-high digital inputs. */
static bool app_gpio_is_high(GPIO_TypeDef *port, uint16_t pin)
{
  return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

static void app_supervisor_apply_led(uint32_t now_ms)
{
  GPIO_PinState led_state = GPIO_PIN_RESET;

  /* LED patterns are intentionally simple so bring-up can be read without a debugger. */
  switch (g_supervisor.status.state) {
  case APP_RUN:
    led_state = GPIO_PIN_SET;
    break;
  case APP_FAULT_HOLD:
    led_state = ((now_ms % 250U) < 125U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    break;
  default:
    led_state = ((now_ms % 500U) < 75U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    break;
  }

  HAL_GPIO_WritePin(g_supervisor.cfg->led_port, g_supervisor.cfg->led_pin, led_state);
}

static void app_supervisor_sample_gpio(void)
{
  /* GPIO sampling is centralized here so both init and runtime use the same interpretation. */
  g_supervisor.status.prochot_asserted =
      app_gpio_is_active_low(g_supervisor.cfg->prochot_port, g_supervisor.cfg->prochot_pin);
  g_supervisor.status.chrg_ok =
      app_gpio_is_high(g_supervisor.cfg->chrg_ok_port, g_supervisor.cfg->chrg_ok_pin);
  g_supervisor.status.vsel1_level =
      app_gpio_is_high(g_supervisor.cfg->vsel1_port, g_supervisor.cfg->vsel1_pin);
  g_supervisor.status.vsel2_level =
      app_gpio_is_high(g_supervisor.cfg->vsel2_port, g_supervisor.cfg->vsel2_pin);
}

static void app_supervisor_set_state(app_state_t next_state, uint32_t now_ms)
{
  /* Record the transition timestamp so later states can enforce dwell and poll intervals. */
  g_supervisor.status.state = next_state;
  g_supervisor.status.last_transition_ms = now_ms;
}

static void app_supervisor_enter_fault(app_fault_code_t fault, uint32_t now_ms)
{
  /* Fault entry is latched by design. The specific code is preserved for post-mortem inspection. */
  g_supervisor.status.last_fault = fault;
  app_supervisor_set_state(APP_FAULT_HOLD, now_ms);
}

static void app_supervisor_update_bq_faults(uint32_t now_ms)
{
  /* BQ status bits are treated as authoritative runtime faults even if I2C is still alive. */
  if (bq25730_snapshot_has_fault(&g_supervisor.status.bq)) {
    app_supervisor_enter_fault(APP_FAULT_BQ_STATUS, now_ms);
  }
}

static void app_supervisor_handle_runtime_poll(uint32_t now_ms)
{
  bq25730_result_t bq_result;
  mpq5031_result_t mpq_result;

  /* Runtime polling always checks the charger first because it is the primary power fault source. */
  bq_result = bq25730_read_snapshot(&g_supervisor.bq_dev, &g_supervisor.status.bq);
  if (bq_result != BQ25730_OK) {
    app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
    return;
  }

  app_supervisor_update_bq_faults(now_ms);
  if (g_supervisor.status.state == APP_FAULT_HOLD) {
    return;
  }

  mpq_result = mpq5031_read_runtime_status(&g_supervisor.mpq_dev, &g_supervisor.status.mpq);
  if (mpq_result != MPQ5031_OK) {
    app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
    return;
  }

  /* MPQ status changes are tracked so the latest baseline can be inspected in the debugger. */
  if ((g_supervisor.status.mpq.status1 != g_supervisor.last_mpq_status1) ||
      (g_supervisor.status.mpq.status2 != g_supervisor.last_mpq_status2)) {
    g_supervisor.last_mpq_status1 = g_supervisor.status.mpq.status1;
    g_supervisor.last_mpq_status2 = g_supervisor.status.mpq.status2;
  }

  g_supervisor.status.last_successful_poll_ms = now_ms;
}

void app_supervisor_init(const board_config_t *cfg)
{
  memset(&g_supervisor, 0, sizeof(g_supervisor));

  /* Initialization only binds the config and devices; all bring-up happens through step(). */
  g_supervisor.cfg = cfg;
  g_supervisor.initialized = true;
  g_supervisor.status.state = APP_BOOT;
  g_supervisor.status.last_fault = APP_FAULT_NONE;

  bq25730_init(&g_supervisor.bq_dev, cfg->i2c, cfg->bq25730_address_7bit);
  mpq5031_init(&g_supervisor.mpq_dev, cfg->i2c, cfg->mpq5031_address_7bit);

  app_supervisor_sample_gpio();
  g_supervisor.last_chrg_ok_sample = g_supervisor.status.chrg_ok;
  HAL_GPIO_WritePin(cfg->led_port, cfg->led_pin, GPIO_PIN_RESET);
}

void app_supervisor_step(uint32_t now_ms)
{
  bq25730_result_t bq_result;
  mpq5031_result_t mpq_result;

  if ((!g_supervisor.initialized) || (g_supervisor.cfg == NULL)) {
    return;
  }

  app_supervisor_sample_gpio();

  /* PROCHOT is checked before any state-specific work so hardware faults preempt bring-up. */
  if (g_supervisor.status.prochot_asserted && (g_supervisor.status.state != APP_FAULT_HOLD)) {
    app_supervisor_enter_fault(APP_FAULT_PROCHOT_ASSERTED, now_ms);
  }

  switch (g_supervisor.status.state) {
  case APP_BOOT:
    /* BOOT is a one-shot entry state used to seed the initial transition timestamp. */
    app_supervisor_set_state(APP_WAIT_POWER_STABLE, now_ms);
    break;

  case APP_WAIT_POWER_STABLE:
    /* The startup delay gives external rails and peripherals time to settle before probing. */
    if ((now_ms - g_supervisor.status.last_transition_ms) >= g_supervisor.cfg->startup_delay_ms) {
      app_supervisor_set_state(APP_PROBE_DEVICES, now_ms);
    }
    break;

  case APP_PROBE_DEVICES:
    /* Device presence is validated before any register reads or writes are attempted. */
    bq_result = bq25730_probe(&g_supervisor.bq_dev, &g_supervisor.status.bq);
    if (bq_result != BQ25730_OK) {
      g_supervisor.status.bq_present = false;
      app_supervisor_enter_fault(APP_FAULT_BQ_NOT_PRESENT, now_ms);
      break;
    }
    g_supervisor.status.bq_present = true;

    mpq_result = mpq5031_probe(&g_supervisor.mpq_dev, &g_supervisor.status.mpq);
    if (mpq_result != MPQ5031_OK) {
      g_supervisor.status.mpq_present = false;
      app_supervisor_enter_fault(APP_FAULT_MPQ_NOT_PRESENT, now_ms);
      break;
    }
    g_supervisor.status.mpq_present = true;
    app_supervisor_set_state(APP_READ_BASELINE, now_ms);
    break;

  case APP_READ_BASELINE:
    /* Baseline reads capture pre-configuration state for later debugging and validation. */
    bq_result = bq25730_read_snapshot(&g_supervisor.bq_dev, &g_supervisor.status.bq);
    if (bq_result != BQ25730_OK) {
      app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
      break;
    }

    mpq_result = mpq5031_read_baseline(&g_supervisor.mpq_dev, &g_supervisor.status.mpq);
    if (mpq_result != MPQ5031_OK) {
      app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
      break;
    }

    g_supervisor.last_mpq_status1 = g_supervisor.status.mpq.status1;
    g_supervisor.last_mpq_status2 = g_supervisor.status.mpq.status2;
    app_supervisor_update_bq_faults(now_ms);
    if (g_supervisor.status.state != APP_FAULT_HOLD) {
      app_supervisor_set_state(APP_CONFIGURE_BQ, now_ms);
    }
    break;

  case APP_CONFIGURE_BQ:
    /* BQ setup is kept separate from MPQ setup so each device has a distinct fault classification. */
    bq_result = bq25730_configure_phase1(&g_supervisor.bq_dev, g_supervisor.cfg, &g_supervisor.status.bq);
    if (bq_result == BQ25730_ERROR_VERIFY) {
      app_supervisor_enter_fault(APP_FAULT_CONFIG_VERIFY, now_ms);
      break;
    }
    if (bq_result == BQ25730_ERROR_I2C) {
      app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
      break;
    }
    if (bq_result != BQ25730_OK) {
      app_supervisor_enter_fault(APP_FAULT_BQ_CONFIG, now_ms);
      break;
    }

    app_supervisor_update_bq_faults(now_ms);
    if (g_supervisor.status.state != APP_FAULT_HOLD) {
      app_supervisor_set_state(APP_CONFIGURE_MPQ, now_ms);
    }
    break;

  case APP_CONFIGURE_MPQ:
    /* MPQ setup runs after the charger so the USB-C side sees the intended power policy. */
    mpq_result =
        mpq5031_configure_phase1(&g_supervisor.mpq_dev, g_supervisor.cfg, &g_supervisor.status.mpq);
    if (mpq_result == MPQ5031_ERROR_VERIFY) {
      app_supervisor_enter_fault(APP_FAULT_CONFIG_VERIFY, now_ms);
      break;
    }
    if (mpq_result == MPQ5031_ERROR_I2C) {
      app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
      break;
    }
    if (mpq_result != MPQ5031_OK) {
      app_supervisor_enter_fault(APP_FAULT_MPQ_CONFIG, now_ms);
      break;
    }

    g_supervisor.last_mpq_status1 = g_supervisor.status.mpq.status1;
    g_supervisor.last_mpq_status2 = g_supervisor.status.mpq.status2;
    g_supervisor.status.last_successful_poll_ms = now_ms;
    app_supervisor_set_state(APP_RUN, now_ms);
    break;

  case APP_RUN:
    /* RUN uses the fast poll interval because the system is considered operational here. */
    if ((now_ms - g_supervisor.status.last_successful_poll_ms) >= g_supervisor.cfg->run_poll_interval_ms) {
      app_supervisor_handle_runtime_poll(now_ms);
    }
    break;

  case APP_FAULT_HOLD:
    /* Fault hold keeps refreshing snapshots, but at a slower cadence to reduce bus traffic. */
    if ((now_ms - g_supervisor.status.last_successful_poll_ms) >= g_supervisor.cfg->fault_poll_interval_ms) {
      (void)bq25730_read_snapshot(&g_supervisor.bq_dev, &g_supervisor.status.bq);
      (void)mpq5031_read_runtime_status(&g_supervisor.mpq_dev, &g_supervisor.status.mpq);
      g_supervisor.status.last_successful_poll_ms = now_ms;
    }
    break;

  default:
    app_supervisor_enter_fault(APP_FAULT_I2C, now_ms);
    break;
  }

  if (g_supervisor.status.chrg_ok != g_supervisor.last_chrg_ok_sample) {
    g_supervisor.last_chrg_ok_sample = g_supervisor.status.chrg_ok;
  }

  app_supervisor_apply_led(now_ms);
}

const app_supervisor_status_t *app_supervisor_get_status(void)
{
  /* Exposed as a read-only view for debugger watches and validation tooling. */
  return &g_supervisor.status;
}
