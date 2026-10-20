/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include "battery_voltage.h"

static uint8_t regs[256];
static uint32_t tick;
static unsigned transactions, writes, fail_on;
static I2C_HandleTypeDef bus;

uint32_t HAL_GetTick(void) { return tick; }

static int transfer(uint16_t address, uint16_t reg, uint16_t width,
                    uint16_t size, uint32_t timeout)
{
    assert(address == (0x6B << 1));
    assert(reg < 256 && width == I2C_MEMADD_SIZE_8BIT && size == 1);
    assert(timeout == 20);
    return ++transactions != fail_on;
}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t a,
    uint16_t reg, uint16_t width, uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(h == &bus);
    if (!transfer(a, reg, width, size, timeout)) return HAL_ERROR;
    *data = regs[reg];
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t a,
    uint16_t reg, uint16_t width, uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(h == &bus);
    if (!transfer(a, reg, width, size, timeout)) return HAL_ERROR;
    assert(reg == 0x01 || reg == 0x3A || reg == 0x3B);
    regs[reg] = *data;
    ++writes;
    return HAL_OK;
}
static void reset(uint32_t start)
{
    for (unsigned i = 0; i < 256; ++i) regs[i] = 0;
    regs[0x2E] = 0x40; regs[0x2F] = 0xD5;
    regs[0x00] = 0x0E; regs[0x01] = 0xE7;
    regs[0x3A] = 0xA4; regs[0x3B] = 0xA0;
    tick = start; transactions = writes = fail_on = 0;
    BatteryVoltage_Init(&bus);
}
static void start_conversion(void)
{
    BatteryVoltage_Poll();
    assert(regs[0x01] == 0x67 && regs[0x00] == 0x0E);
    tick += 25;
    BatteryVoltage_Poll();
    assert(regs[0x3A] == 0xA5 && regs[0x3B] == 0x60);
}
static void complete(uint8_t raw)
{
    regs[0x2C] = raw;
    regs[0x3B] &= 0xBF;
    tick += 200;
    BatteryVoltage_Poll();
}
int main(void)
{
    reset(0);
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_NOT_READY);
    start_conversion();
    tick += 200;
    BatteryVoltage_Poll(); /* START still set: cached result must not be read. */
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_NOT_READY);
    complete(21);
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_OK);
    assert(BatteryVoltage_Get().millivolts == 4224);
    tick += 2001;
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_STALE);
    assert(BatteryVoltage_Get().millivolts == 0);

    reset(0); regs[0x2F] = 0;
    BatteryVoltage_Poll();
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_WRONG_DEVICE);
    assert(writes == 0);
    reset(0); regs[0x2E] = 0;
    BatteryVoltage_Poll();
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_WRONG_DEVICE);
    assert(writes == 0);

    reset(0); start_conversion();
    tick += 501; BatteryVoltage_Poll();
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_TIMEOUT);

    for (unsigned corruption = 0; corruption < 3; ++corruption) {
        reset(0); start_conversion();
        if (corruption == 0) regs[0x01] |= 0x80;
        if (corruption == 1) regs[0x3A] &= 0xFE;
        if (corruption == 2) regs[0x3B] |= 0x80;
        complete(21);
        assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_CONFIG_ERROR);
    }
    /* Every transaction in the successful 12-transaction path can fail. */
    for (unsigned failed = 1; failed <= 12; ++failed) {
        reset(0); fail_on = failed;
        BatteryVoltage_Poll();
        tick = 25; BatteryVoltage_Poll();
        regs[0x3B] &= 0xBF; tick = 225; BatteryVoltage_Poll();
        assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_I2C_ERROR);
        assert(BatteryVoltage_Get().millivolts == 0);
        assert(battery_voltage_errors == 1);
    }
    reset(0); fail_on = 1; BatteryVoltage_Poll();
    tick = 999; BatteryVoltage_Poll(); assert(transactions == 1);
    tick = 1000; BatteryVoltage_Poll(); tick += 25; BatteryVoltage_Poll();
    complete(18);
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_OK);
    assert(BatteryVoltage_Get().millivolts == 4032);
    tick = 2000; fail_on = transactions + 1; BatteryVoltage_Poll();
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_I2C_ERROR);
    assert(BatteryVoltage_Get().millivolts == 0); /* No stale successful value. */

    reset(0); start_conversion(); complete(0);
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_AT_RANGE_LIMIT);
    assert(BatteryVoltage_Get().millivolts == 2880);
    reset(0); start_conversion(); complete(255);
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_AT_RANGE_LIMIT);
    assert(BatteryVoltage_Get().millivolts == 19200);

    reset(0xFFFFFF80U); start_conversion(); complete(18);
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_OK);
    tick += 2001;
    assert(BatteryVoltage_Get().status == BATTERY_VOLTAGE_STALE);
    return 0;
}
