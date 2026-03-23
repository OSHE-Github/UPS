#include "mpq5031.h"

#include <string.h>

/* Register and bit definitions mirror the MPQ5031 programming model. */
#define MPQ5031_REG_PDO_TYPE 0x00
#define MPQ5031_REG_PDO_I1   0x02
#define MPQ5031_REG_CTL1     0x0B
#define MPQ5031_REG_CTL2     0x0C
#define MPQ5031_REG_CTL3     0x0D
#define MPQ5031_REG_CTL4     0x0E
#define MPQ5031_REG_STATUS1  0x10
#define MPQ5031_REG_STATUS2  0x11
#define MPQ5031_REG_ID       0x12

#define MPQ5031_TIMEOUT_MS 50U

#define MPQ5031_VENDOR_ID_MASK  ((uint16_t)0xF000U)
#define MPQ5031_VENDOR_ID_VALUE ((uint16_t)0x8000U)

#define MPQ5031_PDO_TYPE_DISABLE_MASK ((uint16_t)0x00FFU)

#define MPQ5031_CTL1_VBATT_LOW_PULL_PS_EN  ((uint16_t)(1U << 15))
#define MPQ5031_CTL1_NTC2_PS_EN            ((uint16_t)(1U << 13))
#define MPQ5031_CTL1_CDP_EN                ((uint16_t)(1U << 12))
#define MPQ5031_CTL1_VBATT_LOW_PULL_NTC_EN ((uint16_t)(1U << 8))
#define MPQ5031_CTL1_TYPE_C_MODE           ((uint16_t)(1U << 3))

#define MPQ5031_CTL2_SEND_SRC_CAP  ((uint16_t)(1U << 14))
#define MPQ5031_CTL2_RESERVED_BIT2 ((uint16_t)(1U << 2))
#define MPQ5031_CTL2_VDRV_EN       ((uint16_t)(1U << 0))

#define MPQ5031_CTL3_GPIO1_MASK  ((uint16_t)0x0007U)
#define MPQ5031_CTL3_GPIO1_VSEL1 ((uint16_t)0x0004U)

#define MPQ5031_CTL4_VBUS_UV_THD   ((uint16_t)(1U << 14))
#define MPQ5031_CTL4_VDRV_MASK     ((uint16_t)(3U << 9))
#define MPQ5031_CTL4_GPIO6_MASK    ((uint16_t)(7U << 3))
#define MPQ5031_CTL4_GPIO6_VSEL2   ((uint16_t)(1U << 3))
#define MPQ5031_CTL4_GPIO5_MASK    ((uint16_t)0x0007U)

static uint16_t mpq5031_hal_address(const mpq5031_t *dev)
{
  /* HAL uses the 8-bit bus address form, so shift the configured 7-bit address. */
  return (uint16_t)(dev->address_7bit << 1);
}

static mpq5031_result_t mpq5031_read_word(const mpq5031_t *dev, uint8_t reg, uint16_t *value)
{
  uint8_t raw[2];

  if ((dev == NULL) || (dev->i2c == NULL) || (value == NULL)) {
    return MPQ5031_ERROR_INVALID_ARG;
  }

  if (HAL_I2C_Mem_Read(dev->i2c,
                       mpq5031_hal_address(dev),
                       reg,
                       I2C_MEMADD_SIZE_8BIT,
                       raw,
                       2U,
                       MPQ5031_TIMEOUT_MS) != HAL_OK) {
    return MPQ5031_ERROR_I2C;
  }

  *value = (uint16_t)(((uint16_t)raw[1] << 8) | raw[0]);
  return MPQ5031_OK;
}

static mpq5031_result_t mpq5031_write_word(const mpq5031_t *dev, uint8_t reg, uint16_t value)
{
  uint8_t raw[2];

  if ((dev == NULL) || (dev->i2c == NULL)) {
    return MPQ5031_ERROR_INVALID_ARG;
  }

  raw[0] = (uint8_t)(value & 0xFFU);
  raw[1] = (uint8_t)((value >> 8) & 0xFFU);

  if (HAL_I2C_Mem_Write(dev->i2c,
                        mpq5031_hal_address(dev),
                        reg,
                        I2C_MEMADD_SIZE_8BIT,
                        raw,
                        2U,
                        MPQ5031_TIMEOUT_MS) != HAL_OK) {
    return MPQ5031_ERROR_I2C;
  }

  return MPQ5031_OK;
}

static mpq5031_result_t mpq5031_write_and_verify(const mpq5031_t *dev,
                                                 uint8_t reg,
                                                 uint16_t expected,
                                                 uint16_t mask)
{
  uint16_t readback = 0U;
  mpq5031_result_t result;

  result = mpq5031_write_word(dev, reg, expected);
  if (result != MPQ5031_OK) {
    return result;
  }

  result = mpq5031_read_word(dev, reg, &readback);
  if (result != MPQ5031_OK) {
    return result;
  }

  /* Only masked bits are compared because some unrelated bits are preserved by design. */
  return (((readback ^ expected) & mask) == 0U) ? MPQ5031_OK : MPQ5031_ERROR_VERIFY;
}

static mpq5031_result_t mpq5031_read_status_read_clear(mpq5031_t *dev, mpq5031_snapshot_t *snapshot)
{
  mpq5031_result_t result;

  /* STATUS registers are read-clear, so the first sample is preserved for later inspection. */
  result = mpq5031_read_word(dev, MPQ5031_REG_STATUS1, &snapshot->status1);
  if (result != MPQ5031_OK) {
    return result;
  }

  result = mpq5031_read_word(dev, MPQ5031_REG_STATUS2, &snapshot->status2);
  if (result != MPQ5031_OK) {
    return result;
  }

  if (!dev->first_read_clear_valid) {
    dev->first_read_clear_valid = true;
    dev->first_status1 = snapshot->status1;
    dev->first_status2 = snapshot->status2;
  }

  snapshot->first_status_valid = dev->first_read_clear_valid;
  snapshot->first_status1 = dev->first_status1;
  snapshot->first_status2 = dev->first_status2;
  return MPQ5031_OK;
}

void mpq5031_init(mpq5031_t *dev, I2C_HandleTypeDef *i2c, uint8_t address_7bit)
{
  if (dev == NULL) {
    return;
  }

  dev->i2c = i2c;
  dev->address_7bit = address_7bit;
  dev->first_read_clear_valid = false;
  dev->first_status1 = 0U;
  dev->first_status2 = 0U;
}

mpq5031_result_t mpq5031_probe(const mpq5031_t *dev, mpq5031_snapshot_t *snapshot)
{
  uint16_t id = 0U;
  mpq5031_result_t result;

  /* Probe is based on the vendor ID nibble rather than a full fixed device code. */
  result = mpq5031_read_word(dev, MPQ5031_REG_ID, &id);
  if (result != MPQ5031_OK) {
    return result;
  }

  if (snapshot != NULL) {
    snapshot->id = id;
  }

  return ((id & MPQ5031_VENDOR_ID_MASK) == MPQ5031_VENDOR_ID_VALUE) ? MPQ5031_OK
                                                                     : MPQ5031_ERROR_VERIFY;
}

mpq5031_result_t mpq5031_read_baseline(mpq5031_t *dev, mpq5031_snapshot_t *snapshot)
{
  mpq5031_result_t result;

  if ((dev == NULL) || (snapshot == NULL)) {
    return MPQ5031_ERROR_INVALID_ARG;
  }

  memset(snapshot, 0, sizeof(*snapshot));

  result = mpq5031_probe(dev, snapshot);
  if (result != MPQ5031_OK) {
    return result;
  }

  /* Baseline captures both sticky status and the control registers before configuration. */
  result = mpq5031_read_status_read_clear(dev, snapshot);
  if (result != MPQ5031_OK) {
    return result;
  }

  result = mpq5031_read_word(dev, MPQ5031_REG_PDO_TYPE, &snapshot->pdo_type);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_PDO_I1, &snapshot->pdo_i1);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL1, &snapshot->ctl1);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL2, &snapshot->ctl2);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL3, &snapshot->ctl3);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL4, &snapshot->ctl4);
  if (result != MPQ5031_OK) {
    return result;
  }

  return MPQ5031_OK;
}

mpq5031_result_t mpq5031_read_runtime_status(mpq5031_t *dev, mpq5031_snapshot_t *snapshot)
{
  mpq5031_result_t result;

  if ((dev == NULL) || (snapshot == NULL)) {
    return MPQ5031_ERROR_INVALID_ARG;
  }

  /* Runtime reads intentionally match baseline coverage so comparisons are straightforward. */
  result = mpq5031_probe(dev, snapshot);
  if (result != MPQ5031_OK) {
    return result;
  }

  result = mpq5031_read_status_read_clear(dev, snapshot);
  if (result != MPQ5031_OK) {
    return result;
  }

  result = mpq5031_read_word(dev, MPQ5031_REG_PDO_TYPE, &snapshot->pdo_type);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_PDO_I1, &snapshot->pdo_i1);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL1, &snapshot->ctl1);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL2, &snapshot->ctl2);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL3, &snapshot->ctl3);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL4, &snapshot->ctl4);
  if (result != MPQ5031_OK) {
    return result;
  }

  return MPQ5031_OK;
}

mpq5031_result_t mpq5031_configure_phase1(mpq5031_t *dev,
                                          const board_config_t *cfg,
                                          mpq5031_snapshot_t *snapshot)
{
  mpq5031_result_t result;
  uint16_t pdo_type = 0U;
  uint16_t pdo_i1 = 0U;
  uint16_t ctl1 = 0U;
  uint16_t ctl2 = 0U;
  uint16_t ctl3 = 0U;
  uint16_t ctl4 = 0U;

  if ((dev == NULL) || (cfg == NULL)) {
    return MPQ5031_ERROR_INVALID_ARG;
  }

  result = mpq5031_read_word(dev, MPQ5031_REG_PDO_TYPE, &pdo_type);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_PDO_I1, &pdo_i1);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL1, &ctl1);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL2, &ctl2);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL3, &ctl3);
  if (result != MPQ5031_OK) {
    return result;
  }
  result = mpq5031_read_word(dev, MPQ5031_REG_CTL4, &ctl4);
  if (result != MPQ5031_OK) {
    return result;
  }

  /* Enable PDO1 output by clearing the disable bits. */
  pdo_type &= (uint16_t)~MPQ5031_PDO_TYPE_DISABLE_MASK;
  result = mpq5031_write_and_verify(dev, MPQ5031_REG_PDO_TYPE, pdo_type, MPQ5031_PDO_TYPE_DISABLE_MASK);
  if (result != MPQ5031_OK) {
    return result;
  }

  /* Apply the configured advertised current while leaving unrelated upper bits intact. */
  pdo_i1 = (uint16_t)((pdo_i1 & 0xFC00U) | (cfg->mpq_pdo1_current_ma / 10U));
  result = mpq5031_write_and_verify(dev, MPQ5031_REG_PDO_I1, pdo_i1, (uint16_t)0x03FFU);
  if (result != MPQ5031_OK) {
    return result;
  }

  /* Clear unsupported power-path side features for the current phase-1 policy. */
  ctl1 &= (uint16_t)~(MPQ5031_CTL1_VBATT_LOW_PULL_PS_EN |
                      MPQ5031_CTL1_NTC2_PS_EN |
                      MPQ5031_CTL1_CDP_EN |
                      MPQ5031_CTL1_VBATT_LOW_PULL_NTC_EN |
                      MPQ5031_CTL1_TYPE_C_MODE);
  result = mpq5031_write_and_verify(dev,
                                    MPQ5031_REG_CTL1,
                                    ctl1,
                                    (uint16_t)(MPQ5031_CTL1_VBATT_LOW_PULL_PS_EN |
                                               MPQ5031_CTL1_NTC2_PS_EN |
                                               MPQ5031_CTL1_CDP_EN |
                                               MPQ5031_CTL1_VBATT_LOW_PULL_NTC_EN |
                                               MPQ5031_CTL1_TYPE_C_MODE));
  if (result != MPQ5031_OK) {
    return result;
  }

  /* Route GPIO1 and GPIO6 to VSEL outputs so the MCU can observe the negotiated mode. */
  ctl3 &= (uint16_t)~MPQ5031_CTL3_GPIO1_MASK;
  ctl3 |= MPQ5031_CTL3_GPIO1_VSEL1;
  result = mpq5031_write_and_verify(dev, MPQ5031_REG_CTL3, ctl3, MPQ5031_CTL3_GPIO1_MASK);
  if (result != MPQ5031_OK) {
    return result;
  }

  ctl4 &= (uint16_t)~(MPQ5031_CTL4_VBUS_UV_THD |
                      MPQ5031_CTL4_VDRV_MASK |
                      MPQ5031_CTL4_GPIO6_MASK |
                      MPQ5031_CTL4_GPIO5_MASK);
  ctl4 |= (uint16_t)(MPQ5031_CTL4_VBUS_UV_THD | MPQ5031_CTL4_GPIO6_VSEL2);
  result = mpq5031_write_and_verify(dev,
                                    MPQ5031_REG_CTL4,
                                    ctl4,
                                    (uint16_t)(MPQ5031_CTL4_VBUS_UV_THD |
                                               MPQ5031_CTL4_VDRV_MASK |
                                               MPQ5031_CTL4_GPIO6_MASK |
                                               MPQ5031_CTL4_GPIO5_MASK));
  if (result != MPQ5031_OK) {
    return result;
  }

  /* VBUS thresholding and VDRV enable are part of the minimum source-mode bring-up. */
  ctl2 &= (uint16_t)~MPQ5031_CTL2_SEND_SRC_CAP;
  ctl2 &= (uint16_t)~MPQ5031_CTL2_RESERVED_BIT2;
  ctl2 |= MPQ5031_CTL2_VDRV_EN;
  result = mpq5031_write_and_verify(dev,
                                    MPQ5031_REG_CTL2,
                                    ctl2,
                                    (uint16_t)(MPQ5031_CTL2_RESERVED_BIT2 | MPQ5031_CTL2_VDRV_EN));
  if (result != MPQ5031_OK) {
    return result;
  }

  /* SEND_SRC_CAP is written last because it triggers the source capability advertisement. */
  result = mpq5031_write_word(dev, MPQ5031_REG_CTL2, (uint16_t)(ctl2 | MPQ5031_CTL2_SEND_SRC_CAP));
  if (result != MPQ5031_OK) {
    return result;
  }

  /* The part needs a short delay before CTL2 settles after source-cap transmission is requested. */
  HAL_Delay(2U);

  result = mpq5031_read_word(dev, MPQ5031_REG_CTL2, &ctl2);
  if (result != MPQ5031_OK) {
    return result;
  }
  if ((ctl2 & MPQ5031_CTL2_VDRV_EN) == 0U) {
    return MPQ5031_ERROR_VERIFY;
  }
  if ((ctl2 & MPQ5031_CTL2_RESERVED_BIT2) != 0U) {
    return MPQ5031_ERROR_VERIFY;
  }

  if (snapshot != NULL) {
    result = mpq5031_read_runtime_status(dev, snapshot);
    if (result != MPQ5031_OK) {
      return result;
    }
  }

  return MPQ5031_OK;
}
