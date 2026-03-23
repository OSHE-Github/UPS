#include "bq25730.h"

#include <string.h>

/* Register numbers come directly from the BQ25730 programming model. */
#define BQ25730_MANUFACTURER_ID_REG 0x2E
#define BQ25730_DEVICE_ID_REG       0x2F

#define BQ25730_REG_CHARGE_OPTION0  0x01
#define BQ25730_REG_CHARGE_CURRENT  0x03
#define BQ25730_REG_CHARGE_VOLTAGE  0x05
#define BQ25730_REG_INPUT_VOLTAGE   0x0B
#define BQ25730_REG_IIN_HOST        0x0F
#define BQ25730_REG_CHARGER_STATUS  0x21
#define BQ25730_REG_PROCHOT_STATUS  0x23
#define BQ25730_REG_CHARGE_OPTION1  0x31

#define BQ25730_OPTION0_EN_LWPWR       ((uint16_t)(1U << 15))
#define BQ25730_OPTION0_WDT_MASK       ((uint16_t)(3U << 13))
#define BQ25730_OPTION0_VSYS_UVP_ENZ   ((uint16_t)(1U << 6))
#define BQ25730_OPTION0_EN_LDO         ((uint16_t)(1U << 2))
#define BQ25730_OPTION0_EN_IIN_DPM     ((uint16_t)(1U << 1))
#define BQ25730_OPTION0_CHRG_INHIBIT   ((uint16_t)(1U << 0))

#define BQ25730_OPTION1_EN_PROCHOT_LPWR ((uint16_t)(1U << 14))
#define BQ25730_OPTION1_RSNS_RAC        ((uint16_t)(1U << 11))
#define BQ25730_OPTION1_RSNS_RSR        ((uint16_t)(1U << 10))
#define BQ25730_OPTION1_FORCE_CONV_OFF  ((uint16_t)(1U << 3))
#define BQ25730_OPTION1_EN_PTM          ((uint16_t)(1U << 2))

#define BQ25730_CHARGER_STATUS_FAULT_MASK ((uint16_t)0x00FFU)

#define BQ25730_MANUFACTURER_ID_VALUE 0x40U
#define BQ25730_DEVICE_ID_VALUE       0xD5U

#define BQ25730_TIMEOUT_MS            50U

static uint16_t bq25730_hal_address(const bq25730_t *dev)
{
  /* HAL expects the 7-bit address left-shifted into the wire-format position. */
  return (uint16_t)(dev->address_7bit << 1);
}

static bq25730_result_t bq25730_read_u8(const bq25730_t *dev, uint8_t reg, uint8_t *value)
{
  if ((dev == NULL) || (dev->i2c == NULL) || (value == NULL)) {
    return BQ25730_ERROR_INVALID_ARG;
  }

  if (HAL_I2C_Mem_Read(dev->i2c,
                       bq25730_hal_address(dev),
                       reg,
                       I2C_MEMADD_SIZE_8BIT,
                       value,
                       1U,
                       BQ25730_TIMEOUT_MS) != HAL_OK) {
    return BQ25730_ERROR_I2C;
  }

  return BQ25730_OK;
}

static bq25730_result_t bq25730_read_word(const bq25730_t *dev, uint8_t reg_hi, uint16_t *value)
{
  uint8_t raw[2];
  uint8_t reg_lo;

  /* This device exposes 16-bit registers as low-byte / high-byte pairs. */
  if ((dev == NULL) || (dev->i2c == NULL) || (value == NULL) || (reg_hi == 0U)) {
    return BQ25730_ERROR_INVALID_ARG;
  }

  reg_lo = (uint8_t)(reg_hi - 1U);
  if (HAL_I2C_Mem_Read(dev->i2c,
                       bq25730_hal_address(dev),
                       reg_lo,
                       I2C_MEMADD_SIZE_8BIT,
                       raw,
                       2U,
                       BQ25730_TIMEOUT_MS) != HAL_OK) {
    return BQ25730_ERROR_I2C;
  }

  *value = (uint16_t)(((uint16_t)raw[1] << 8) | raw[0]);
  return BQ25730_OK;
}

static bq25730_result_t bq25730_write_word(const bq25730_t *dev, uint8_t reg_hi, uint16_t value)
{
  uint8_t raw[2];
  uint8_t reg_lo;

  /* Writes mirror the same little-endian register ordering used by reads. */
  if ((dev == NULL) || (dev->i2c == NULL) || (reg_hi == 0U)) {
    return BQ25730_ERROR_INVALID_ARG;
  }

  reg_lo = (uint8_t)(reg_hi - 1U);
  raw[0] = (uint8_t)(value & 0xFFU);
  raw[1] = (uint8_t)((value >> 8) & 0xFFU);

  if (HAL_I2C_Mem_Write(dev->i2c,
                        bq25730_hal_address(dev),
                        reg_lo,
                        I2C_MEMADD_SIZE_8BIT,
                        raw,
                        2U,
                        BQ25730_TIMEOUT_MS) != HAL_OK) {
    return BQ25730_ERROR_I2C;
  }

  return BQ25730_OK;
}

static bq25730_result_t bq25730_write_and_verify_word(const bq25730_t *dev,
                                                       uint8_t reg_hi,
                                                       uint16_t expected)
{
  uint16_t readback = 0U;
  bq25730_result_t result;

  result = bq25730_write_word(dev, reg_hi, expected);
  if (result != BQ25730_OK) {
    return result;
  }

  result = bq25730_read_word(dev, reg_hi, &readback);
  if (result != BQ25730_OK) {
    return result;
  }

  return (readback == expected) ? BQ25730_OK : BQ25730_ERROR_VERIFY;
}

static uint16_t bq25730_encode_charge_voltage_mv(uint16_t millivolts)
{
  /* Clamp to the device-supported range before converting into register units. */
  if (millivolts < 1024U) {
    millivolts = 1024U;
  }
  if (millivolts > 23000U) {
    millivolts = 23000U;
  }

  return (uint16_t)((millivolts - 1024U) / 8U);
}

static uint16_t bq25730_encode_input_voltage_mv(uint16_t millivolts)
{
  /* Input voltage limit uses a different range and step size than charge voltage. */
  if (millivolts < 3200U) {
    millivolts = 3200U;
  }
  if (millivolts > 19520U) {
    millivolts = 19520U;
  }

  return (uint16_t)((millivolts - 3200U) / 64U);
}

static uint16_t bq25730_encode_charge_current_ma(uint16_t milliamps, uint16_t rsr_milliohm)
{
  /* Current encoding depends on the configured sense resistor value on the board. */
  uint16_t step_ma = (rsr_milliohm == 10U) ? 64U : 128U;

  return (uint16_t)((milliamps + (step_ma / 2U)) / step_ma);
}

static uint16_t bq25730_encode_iin_host_ma(uint16_t milliamps, uint16_t rac_milliohm)
{
  /* Input current limit also scales with the board's input sense resistor. */
  if (rac_milliohm == 10U) {
    if (milliamps <= 50U) {
      return 0U;
    }
    return (uint16_t)(milliamps / 50U);
  }

  if (milliamps <= 100U) {
    return 0U;
  }
  return (uint16_t)(milliamps / 100U);
}

void bq25730_init(bq25730_t *dev, I2C_HandleTypeDef *i2c, uint8_t address_7bit)
{
  if (dev == NULL) {
    return;
  }

  dev->i2c = i2c;
  dev->address_7bit = address_7bit;
}

bq25730_result_t bq25730_probe(const bq25730_t *dev, bq25730_snapshot_t *snapshot)
{
  bq25730_result_t result;
  uint8_t manufacturer = 0U;
  uint8_t device_id = 0U;

  /* Probe is intentionally cheap: only enough reads to prove the expected part is present. */
  result = bq25730_read_u8(dev, BQ25730_MANUFACTURER_ID_REG, &manufacturer);
  if (result != BQ25730_OK) {
    return result;
  }

  result = bq25730_read_u8(dev, BQ25730_DEVICE_ID_REG, &device_id);
  if (result != BQ25730_OK) {
    return result;
  }

  if (snapshot != NULL) {
    snapshot->manufacturer_id = manufacturer;
    snapshot->device_id = device_id;
    snapshot->device_id_valid =
        (manufacturer == BQ25730_MANUFACTURER_ID_VALUE) && (device_id == BQ25730_DEVICE_ID_VALUE);
  }

  return ((manufacturer == BQ25730_MANUFACTURER_ID_VALUE) &&
          (device_id == BQ25730_DEVICE_ID_VALUE))
             ? BQ25730_OK
             : BQ25730_ERROR_VERIFY;
}

bq25730_result_t bq25730_read_snapshot(const bq25730_t *dev, bq25730_snapshot_t *snapshot)
{
  bq25730_result_t result;

  if ((dev == NULL) || (snapshot == NULL)) {
    return BQ25730_ERROR_INVALID_ARG;
  }

  memset(snapshot, 0, sizeof(*snapshot));

  result = bq25730_probe(dev, snapshot);
  if (result != BQ25730_OK) {
    return result;
  }

  /* A snapshot intentionally gathers the full register set the supervisor reasons about. */
  result = bq25730_read_word(dev, BQ25730_REG_CHARGE_OPTION0, &snapshot->charge_option0);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_CHARGE_OPTION1, &snapshot->charge_option1);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_INPUT_VOLTAGE, &snapshot->input_voltage);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_IIN_HOST, &snapshot->iin_host);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_CHARGE_VOLTAGE, &snapshot->charge_voltage);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_CHARGE_CURRENT, &snapshot->charge_current);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_CHARGER_STATUS, &snapshot->charger_status);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_PROCHOT_STATUS, &snapshot->prochot_status);
  if (result != BQ25730_OK) {
    return result;
  }

  snapshot->fault_present = bq25730_snapshot_has_fault(snapshot);
  return BQ25730_OK;
}

bq25730_result_t bq25730_configure_phase1(const bq25730_t *dev,
                                          const board_config_t *cfg,
                                          bq25730_snapshot_t *snapshot)
{
  bq25730_result_t result;
  uint16_t charge_option0 = 0U;
  uint16_t charge_option1 = 0U;
  uint16_t desired_input = 0U;
  uint16_t desired_voltage = 0U;
  uint16_t desired_current = 0U;

  if ((dev == NULL) || (cfg == NULL)) {
    return BQ25730_ERROR_INVALID_ARG;
  }

  result = bq25730_read_word(dev, BQ25730_REG_CHARGE_OPTION0, &charge_option0);
  if (result != BQ25730_OK) {
    return result;
  }
  result = bq25730_read_word(dev, BQ25730_REG_CHARGE_OPTION1, &charge_option1);
  if (result != BQ25730_OK) {
    return result;
  }

  /* Phase 1 turns on the required housekeeping bits while holding charge inhibited initially. */
  charge_option0 &= (uint16_t)~(BQ25730_OPTION0_EN_LWPWR |
                                BQ25730_OPTION0_WDT_MASK |
                                BQ25730_OPTION0_VSYS_UVP_ENZ |
                                BQ25730_OPTION0_EN_LDO |
                                BQ25730_OPTION0_EN_IIN_DPM |
                                BQ25730_OPTION0_CHRG_INHIBIT);
  charge_option0 |= (uint16_t)(BQ25730_OPTION0_EN_LDO |
                               BQ25730_OPTION0_EN_IIN_DPM |
                               BQ25730_OPTION0_CHRG_INHIBIT);
  result = bq25730_write_and_verify_word(dev, BQ25730_REG_CHARGE_OPTION0, charge_option0);
  if (result != BQ25730_OK) {
    return result;
  }

  /* Option1 is cleared down to the intended baseline before any limits are programmed. */
  charge_option1 &= (uint16_t)~(BQ25730_OPTION1_EN_PROCHOT_LPWR |
                                BQ25730_OPTION1_RSNS_RAC |
                                BQ25730_OPTION1_RSNS_RSR |
                                BQ25730_OPTION1_FORCE_CONV_OFF |
                                BQ25730_OPTION1_EN_PTM);
  result = bq25730_write_and_verify_word(dev, BQ25730_REG_CHARGE_OPTION1, charge_option1);
  if (result != BQ25730_OK) {
    return result;
  }

  /* Program the configured current and voltage limits from board_config. */
  desired_input = bq25730_encode_iin_host_ma(cfg->bq_input_current_limit_ma, cfg->bq_rac_milliohm);
  result = bq25730_write_and_verify_word(dev, BQ25730_REG_IIN_HOST, desired_input);
  if (result != BQ25730_OK) {
    return result;
  }

  desired_voltage = bq25730_encode_charge_voltage_mv(cfg->bq_charge_voltage_mv);
  result = bq25730_write_and_verify_word(dev, BQ25730_REG_CHARGE_VOLTAGE, desired_voltage);
  if (result != BQ25730_OK) {
    return result;
  }

  desired_current = bq25730_encode_charge_current_ma(cfg->bq_charge_current_ma, cfg->bq_rsr_milliohm);
  result = bq25730_write_and_verify_word(dev, BQ25730_REG_CHARGE_CURRENT, desired_current);
  if (result != BQ25730_OK) {
    return result;
  }

  result = bq25730_write_and_verify_word(dev,
                                         BQ25730_REG_INPUT_VOLTAGE,
                                         bq25730_encode_input_voltage_mv(cfg->bq_input_voltage_limit_mv));
  if (result != BQ25730_OK) {
    return result;
  }

  /* Charging is only enabled after all dependent limits were written and verified. */
  charge_option0 &= (uint16_t)~BQ25730_OPTION0_CHRG_INHIBIT;
  result = bq25730_write_and_verify_word(dev, BQ25730_REG_CHARGE_OPTION0, charge_option0);
  if (result != BQ25730_OK) {
    return result;
  }

  if (snapshot != NULL) {
    result = bq25730_read_snapshot(dev, snapshot);
    if (result != BQ25730_OK) {
      return result;
    }
  }

  return BQ25730_OK;
}

bool bq25730_snapshot_has_fault(const bq25730_snapshot_t *snapshot)
{
  if (snapshot == NULL) {
    return false;
  }

  /* The low byte of CHARGER_STATUS contains the fault bits the supervisor cares about. */
  return (snapshot->charger_status & BQ25730_CHARGER_STATUS_FAULT_MASK) != 0U;
}
