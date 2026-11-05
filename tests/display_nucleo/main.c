/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Standalone NUCLEO-L152RE display test. Not UPS power-control firmware. */
#include "oled.h"
#ifdef UPS_THERMISTORS
#include "thermistors.h"
#include "temperature_view.h"
#endif
#ifdef UPS_POWER_MOCK
#include "power_mock.h"
#endif

static I2C_HandleTypeDef bus;
/* Watch these in a debugger. A successful transfer is not a visual inspection. */
volatile uint32_t display_frames;
volatile uint32_t display_errors;
volatile HAL_StatusTypeDef display_status = HAL_ERROR;

void SysTick_Handler(void) { HAL_IncTick(); }

static void fatal(void)
{
    for (;;) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(100);
    }
}

static void setup(void)
{
    HAL_Init();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_5; /* Nucleo LD2, not the UPS LED. */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) fatal();
    RCC_ClkInitTypeDef clocks = {0};
    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                      RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV1;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_0) != HAL_OK) fatal();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL; /* Adafruit 4440 supplies external pull-ups. */
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &gpio);
    bus.Instance = I2C2;
    bus.Init.ClockSpeed = 100000;
    bus.Init.DutyCycle = I2C_DUTYCYCLE_2;
    bus.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    bus.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    bus.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    bus.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&bus) != HAL_OK) fatal();
#ifdef UPS_POWER_MOCK
    if (PowerMock_Init() != HAL_OK) fatal();
#endif
    HAL_Delay(200);
#ifdef UPS_THERMISTORS
    (void)Thermistors_Init();
#endif
}

int main(void)
{
    setup();
#if defined(UPS_POWER_MOCK) || defined(UPS_THERMISTORS)
    /* Keep acquiring inputs even when the display is disconnected. */
    uint32_t next_init = 0;
    uint32_t frame = 0;
    for (;;) {
#ifdef UPS_POWER_MOCK
        PowerMock_Poll();
#endif
#ifdef UPS_THERMISTORS
        Thermistors_Poll();
#endif
        if (display_status != HAL_OK && (int32_t)(HAL_GetTick() - next_init) >= 0) {
            HAL_I2C_DeInit(&bus);
            __HAL_RCC_I2C2_FORCE_RESET();
            __HAL_RCC_I2C2_RELEASE_RESET();
            display_status = HAL_I2C_Init(&bus);
            if (display_status == HAL_OK) display_status = OLED_Init(&bus);
            if (display_status != HAL_OK) ++display_errors;
            next_init = HAL_GetTick() + 2000U;
        }
        if (display_status == HAL_OK) {
#if defined(UPS_THERMISTORS) && defined(UPS_POWER_MOCK)
            display_status = (frame / 4U) & 1U ? Temperature_Show(&bus) :
                                               PowerMock_Show(&bus, frame);
#elif defined(UPS_THERMISTORS)
            display_status = Temperature_Show(&bus);
#else
            display_status = PowerMock_Show(&bus, frame);
#endif
            if (display_status == HAL_OK) ++display_frames;
            else ++display_errors;
        }
        ++frame;
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(500);
    }
#else
    for (;;) {
        display_status = OLED_Init(&bus);
        while (display_status == HAL_OK) {
#ifdef UPS_THERMISTORS
            Thermistors_Poll();
            display_status = Temperature_Show(&bus);
#else
            display_status = OLED_TestFrame(&bus, display_frames);
#endif
            if (display_status == HAL_OK) ++display_frames;
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            HAL_Delay(500);
        }
        ++display_errors;
        /* Missing display or a failed transfer never stops the test heartbeat. */
        for (unsigned i = 0; i < 20; ++i) {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            HAL_Delay(100);
        }
        HAL_I2C_DeInit(&bus);
        __HAL_RCC_I2C2_FORCE_RESET();
        __HAL_RCC_I2C2_RELEASE_RESET();
        if (HAL_I2C_Init(&bus) != HAL_OK) fatal();
    }
#endif
}
