/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_supervisor.h"
#include "board_config.h"
#include "validation_debug.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
/* Board config is built once at boot and then shared by the supervisor and validation code. */
static board_config_t g_board_config;
/* Volatile keeps debugger reads honest while the firmware updates this snapshot in the loop. */
volatile validation_debug_snapshot_t g_validation_debug;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
enum {
  /* Magic word used to confirm the validation snapshot is initialized and being read correctly. */
  VALIDATION_DEBUG_MAGIC = 0x55505356U
};

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

#define MPQ5031_PDO_TYPE_DISABLE_MASK ((uint16_t)0x00FFU)
#define MPQ5031_CTL1_VBATT_LOW_PULL_PS_EN  ((uint16_t)(1U << 15))
#define MPQ5031_CTL1_NTC2_PS_EN            ((uint16_t)(1U << 13))
#define MPQ5031_CTL1_CDP_EN                ((uint16_t)(1U << 12))
#define MPQ5031_CTL1_VBATT_LOW_PULL_NTC_EN ((uint16_t)(1U << 8))
#define MPQ5031_CTL1_TYPE_C_MODE           ((uint16_t)(1U << 3))
#define MPQ5031_CTL2_RESERVED_BIT2         ((uint16_t)(1U << 2))
#define MPQ5031_CTL2_VDRV_EN               ((uint16_t)(1U << 0))
#define MPQ5031_CTL3_GPIO1_MASK            ((uint16_t)0x0007U)
#define MPQ5031_CTL3_GPIO1_VSEL1           ((uint16_t)0x0004U)
#define MPQ5031_CTL4_VBUS_UV_THD           ((uint16_t)(1U << 14))
#define MPQ5031_CTL4_VDRV_MASK             ((uint16_t)(3U << 9))
#define MPQ5031_CTL4_GPIO6_MASK            ((uint16_t)(7U << 3))
#define MPQ5031_CTL4_GPIO6_VSEL2           ((uint16_t)(1U << 3))
#define MPQ5031_CTL4_GPIO5_MASK            ((uint16_t)0x0007U)

static const char *validation_state_name(app_state_t state)
{
  /* String forms are easier to inspect in CubeIDE live expressions than raw enum integers. */
  switch (state) {
  case APP_BOOT:
    return "APP_BOOT";
  case APP_WAIT_POWER_STABLE:
    return "APP_WAIT_POWER_STABLE";
  case APP_PROBE_DEVICES:
    return "APP_PROBE_DEVICES";
  case APP_READ_BASELINE:
    return "APP_READ_BASELINE";
  case APP_CONFIGURE_BQ:
    return "APP_CONFIGURE_BQ";
  case APP_CONFIGURE_MPQ:
    return "APP_CONFIGURE_MPQ";
  case APP_RUN:
    return "APP_RUN";
  case APP_FAULT_HOLD:
    return "APP_FAULT_HOLD";
  default:
    return "APP_STATE_UNKNOWN";
  }
}

static const char *validation_fault_name(app_fault_code_t fault)
{
  /* Fault names are exposed for the same debugger-first reason as state names. */
  switch (fault) {
  case APP_FAULT_NONE:
    return "APP_FAULT_NONE";
  case APP_FAULT_PROCHOT_ASSERTED:
    return "APP_FAULT_PROCHOT_ASSERTED";
  case APP_FAULT_BQ_NOT_PRESENT:
    return "APP_FAULT_BQ_NOT_PRESENT";
  case APP_FAULT_MPQ_NOT_PRESENT:
    return "APP_FAULT_MPQ_NOT_PRESENT";
  case APP_FAULT_BQ_CONFIG:
    return "APP_FAULT_BQ_CONFIG";
  case APP_FAULT_MPQ_CONFIG:
    return "APP_FAULT_MPQ_CONFIG";
  case APP_FAULT_BQ_STATUS:
    return "APP_FAULT_BQ_STATUS";
  case APP_FAULT_I2C:
    return "APP_FAULT_I2C";
  case APP_FAULT_CONFIG_VERIFY:
    return "APP_FAULT_CONFIG_VERIFY";
  default:
    return "APP_FAULT_UNKNOWN";
  }
}

static uint16_t validation_encode_bq_charge_voltage_mv(uint16_t millivolts)
{
  /* Mirror the driver-side encoding so debugger checks can compare actual vs expected values. */
  if (millivolts < 1024U) {
    millivolts = 1024U;
  }
  if (millivolts > 23000U) {
    millivolts = 23000U;
  }

  return (uint16_t)((millivolts - 1024U) / 8U);
}

static uint16_t validation_encode_bq_input_voltage_mv(uint16_t millivolts)
{
  /* Validation reproduces the same clamping rules used by the real BQ driver. */
  if (millivolts < 3200U) {
    millivolts = 3200U;
  }
  if (millivolts > 19520U) {
    millivolts = 19520U;
  }

  return (uint16_t)((millivolts - 3200U) / 64U);
}

static uint16_t validation_encode_bq_charge_current_ma(uint16_t milliamps, uint16_t rsr_milliohm)
{
  /* The sense resistor changes the register step size, so validation must account for it. */
  uint16_t step_ma = (rsr_milliohm == 10U) ? 64U : 128U;
  return (uint16_t)((milliamps + (step_ma / 2U)) / step_ma);
}

static uint16_t validation_encode_bq_iin_host_ma(uint16_t milliamps, uint16_t rac_milliohm)
{
  /* Input current encoding follows the same RAC-dependent units as the production driver. */
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

void validation_debug_init(const board_config_t *cfg)
{
  /* Build the expected register values once so runtime capture can stay cheap. */
  g_validation_debug.magic = VALIDATION_DEBUG_MAGIC;
  g_validation_debug.capture_count = 0U;
  g_validation_debug.timestamp_ms = 0U;
  g_validation_debug.initialized = true;
  g_validation_debug.error_handler_entered = false;
  g_validation_debug.state_name = "APP_STATE_UNINITIALIZED";
  g_validation_debug.fault_name = "APP_FAULT_UNKNOWN";
  if (cfg != NULL) {
    g_validation_debug.config = *cfg;
  } else {
    return;
  }

  g_validation_debug.bq_expected.option0_mask = (uint16_t)(BQ25730_OPTION0_EN_LWPWR |
                                                            BQ25730_OPTION0_WDT_MASK |
                                                            BQ25730_OPTION0_VSYS_UVP_ENZ |
                                                            BQ25730_OPTION0_EN_LDO |
                                                            BQ25730_OPTION0_EN_IIN_DPM |
                                                            BQ25730_OPTION0_CHRG_INHIBIT);
  g_validation_debug.bq_expected.option0_value =
      (uint16_t)(BQ25730_OPTION0_EN_LDO | BQ25730_OPTION0_EN_IIN_DPM);
  g_validation_debug.bq_expected.option1_mask = (uint16_t)(BQ25730_OPTION1_EN_PROCHOT_LPWR |
                                                            BQ25730_OPTION1_RSNS_RAC |
                                                            BQ25730_OPTION1_RSNS_RSR |
                                                            BQ25730_OPTION1_FORCE_CONV_OFF |
                                                            BQ25730_OPTION1_EN_PTM);
  g_validation_debug.bq_expected.option1_value = 0U;
  g_validation_debug.bq_expected.charge_current =
      validation_encode_bq_charge_current_ma(cfg->bq_charge_current_ma, cfg->bq_rsr_milliohm);
  g_validation_debug.bq_expected.charge_voltage =
      validation_encode_bq_charge_voltage_mv(cfg->bq_charge_voltage_mv);
  g_validation_debug.bq_expected.input_voltage =
      validation_encode_bq_input_voltage_mv(cfg->bq_input_voltage_limit_mv);
  g_validation_debug.bq_expected.iin_host =
      validation_encode_bq_iin_host_ma(cfg->bq_input_current_limit_ma, cfg->bq_rac_milliohm);

  g_validation_debug.mpq_expected.pdo_type_mask = MPQ5031_PDO_TYPE_DISABLE_MASK;
  g_validation_debug.mpq_expected.pdo_type_value = 0U;
  g_validation_debug.mpq_expected.pdo_i1_mask = (uint16_t)0x03FFU;
  g_validation_debug.mpq_expected.pdo_i1_value = (uint16_t)(cfg->mpq_pdo1_current_ma / 10U);
  g_validation_debug.mpq_expected.ctl1_mask =
      (uint16_t)(MPQ5031_CTL1_VBATT_LOW_PULL_PS_EN |
                 MPQ5031_CTL1_NTC2_PS_EN |
                 MPQ5031_CTL1_CDP_EN |
                 MPQ5031_CTL1_VBATT_LOW_PULL_NTC_EN |
                 MPQ5031_CTL1_TYPE_C_MODE);
  g_validation_debug.mpq_expected.ctl1_value = 0U;
  g_validation_debug.mpq_expected.ctl2_mask = (uint16_t)(MPQ5031_CTL2_RESERVED_BIT2 | MPQ5031_CTL2_VDRV_EN);
  g_validation_debug.mpq_expected.ctl2_value = MPQ5031_CTL2_VDRV_EN;
  g_validation_debug.mpq_expected.ctl3_mask = MPQ5031_CTL3_GPIO1_MASK;
  g_validation_debug.mpq_expected.ctl3_value = MPQ5031_CTL3_GPIO1_VSEL1;
  g_validation_debug.mpq_expected.ctl4_mask =
      (uint16_t)(MPQ5031_CTL4_VBUS_UV_THD |
                 MPQ5031_CTL4_VDRV_MASK |
                 MPQ5031_CTL4_GPIO6_MASK |
                 MPQ5031_CTL4_GPIO5_MASK);
  g_validation_debug.mpq_expected.ctl4_value =
      (uint16_t)(MPQ5031_CTL4_VBUS_UV_THD | MPQ5031_CTL4_GPIO6_VSEL2);
}

void validation_debug_capture(uint32_t now_ms)
{
  const app_supervisor_status_t *status = app_supervisor_get_status();

  if (status == NULL) {
    return;
  }

  /* Copy the supervisor status first, then derive all validation match flags from that snapshot. */
  g_validation_debug.capture_count += 1U;
  g_validation_debug.timestamp_ms = now_ms;
  g_validation_debug.status = *status;
  g_validation_debug.state_name = validation_state_name(status->state);
  g_validation_debug.fault_name = validation_fault_name(status->last_fault);

  g_validation_debug.bq_expected.option0_matches =
      (status->bq.charge_option0 & g_validation_debug.bq_expected.option0_mask) ==
      g_validation_debug.bq_expected.option0_value;
  g_validation_debug.bq_expected.option1_matches =
      (status->bq.charge_option1 & g_validation_debug.bq_expected.option1_mask) ==
      g_validation_debug.bq_expected.option1_value;
  g_validation_debug.bq_expected.charge_current_matches =
      status->bq.charge_current == g_validation_debug.bq_expected.charge_current;
  g_validation_debug.bq_expected.charge_voltage_matches =
      status->bq.charge_voltage == g_validation_debug.bq_expected.charge_voltage;
  g_validation_debug.bq_expected.input_voltage_matches =
      status->bq.input_voltage == g_validation_debug.bq_expected.input_voltage;
  g_validation_debug.bq_expected.iin_host_matches =
      status->bq.iin_host == g_validation_debug.bq_expected.iin_host;
  g_validation_debug.bq_config_matches =
      g_validation_debug.bq_expected.option0_matches &&
      g_validation_debug.bq_expected.option1_matches &&
      g_validation_debug.bq_expected.charge_current_matches &&
      g_validation_debug.bq_expected.charge_voltage_matches &&
      g_validation_debug.bq_expected.input_voltage_matches &&
      g_validation_debug.bq_expected.iin_host_matches;

  g_validation_debug.mpq_expected.pdo_type_matches =
      (status->mpq.pdo_type & g_validation_debug.mpq_expected.pdo_type_mask) ==
      g_validation_debug.mpq_expected.pdo_type_value;
  g_validation_debug.mpq_expected.pdo_i1_matches =
      (status->mpq.pdo_i1 & g_validation_debug.mpq_expected.pdo_i1_mask) ==
      g_validation_debug.mpq_expected.pdo_i1_value;
  g_validation_debug.mpq_expected.ctl1_matches =
      (status->mpq.ctl1 & g_validation_debug.mpq_expected.ctl1_mask) ==
      g_validation_debug.mpq_expected.ctl1_value;
  g_validation_debug.mpq_expected.ctl2_matches =
      (status->mpq.ctl2 & g_validation_debug.mpq_expected.ctl2_mask) ==
      g_validation_debug.mpq_expected.ctl2_value;
  g_validation_debug.mpq_expected.ctl3_matches =
      (status->mpq.ctl3 & g_validation_debug.mpq_expected.ctl3_mask) ==
      g_validation_debug.mpq_expected.ctl3_value;
  g_validation_debug.mpq_expected.ctl4_matches =
      (status->mpq.ctl4 & g_validation_debug.mpq_expected.ctl4_mask) ==
      g_validation_debug.mpq_expected.ctl4_value;
  g_validation_debug.mpq_config_matches =
      g_validation_debug.mpq_expected.pdo_type_matches &&
      g_validation_debug.mpq_expected.pdo_i1_matches &&
      g_validation_debug.mpq_expected.ctl1_matches &&
      g_validation_debug.mpq_expected.ctl2_matches &&
      g_validation_debug.mpq_expected.ctl3_matches &&
      g_validation_debug.mpq_expected.ctl4_matches;

  g_validation_debug.gpio_vsel_matches = status->vsel1_level && status->vsel2_level;
  if ((status->state == APP_RUN) && (status->last_fault == APP_FAULT_NONE)) {
    /* Sticky flag so validation can prove the system reached nominal runtime at least once. */
    g_validation_debug.nominal_run_reached = true;
  }
}

void validation_debug_note_error_handler(void)
{
  /* Error_Handler stops normal execution, so this flag is the last breadcrumb before the halt. */
  g_validation_debug.error_handler_entered = true;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  /* Bring up the application after CubeMX-generated peripheral init is complete. */
  g_board_config = board_config_make_phase1(&hi2c1);
  app_supervisor_init(&g_board_config);
  validation_debug_init(&g_board_config);
  validation_debug_capture(HAL_GetTick());

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* The firmware is intentionally polling-driven for first bring-up and validation simplicity. */
    app_supervisor_step(HAL_GetTick());
    validation_debug_capture(HAL_GetTick());
    HAL_Delay(10);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_5;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  /* 100 kHz standard-mode I2C is used for initial bring-up margin and easier bus debugging. */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PROCHOT_N_Pin CHRG_OK_Pin VSEL1_Pin VSEL2_Pin */
  /* Inputs are left floating here because the intended PCB is expected to provide the biasing. */
  GPIO_InitStruct.Pin = PROCHOT_N_Pin|CHRG_OK_Pin|VSEL1_Pin|VSEL2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  validation_debug_note_error_handler();
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
