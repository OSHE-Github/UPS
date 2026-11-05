/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "oled.h"
#include <string.h>

#define OLED_ADDR (0x3cU << 1)
#define TIMEOUT_MS 20U
static uint8_t pixels[512];

/* Five columns, least significant bit at the top. */
static const uint8_t digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30}, {1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36}, {6,0x49,0x49,0x29,0x1e}
};
static const uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,8,8,8,0x7f},
    {0,0x41,0x7f,0x41,0}, {0x20,0x40,0x41,0x3f,1},
    {0x7f,8,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f}, {0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f}, {0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43}
};

static HAL_StatusTypeDef send(I2C_HandleTypeDef *bus, uint8_t control,
                              const uint8_t *data, uint16_t length)
{
    uint8_t packet[33];
    if (length > 32U) return HAL_ERROR;
    packet[0] = control;
    memcpy(packet + 1, data, length);
    return HAL_I2C_Master_Transmit(bus, OLED_ADDR, packet, length + 1U, TIMEOUT_MS);
}

static HAL_StatusTypeDef flush(I2C_HandleTypeDef *bus)
{
    const uint8_t window[] = {0x21,0,127,0x22,0,3};
    HAL_StatusTypeDef result = send(bus, 0, window, sizeof window);
    for (uint16_t offset = 0; result == HAL_OK && offset < sizeof pixels; offset += 32U)
        result = send(bus, 0x40, pixels + offset, 32);
    return result;
}

HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *bus)
{
    /* Internal charge pump, 32 multiplexed rows, horizontal addressing.
     * Auto-reset is on the Adafruit module; allow it to finish before probing.
     */
    static const uint8_t init[] = {
        0xae,0xd5,0x80,0xa8,0x1f,0xd3,0,0x40,0x8d,0x14,
        0x20,0,0xa1,0xc8,0xda,0x02,0x81,0x7f,0xd9,0xf1,
        0xdb,0x40,0xa4,0xa6,0x2e
    };
    HAL_StatusTypeDef result = HAL_I2C_IsDeviceReady(bus, OLED_ADDR, 1, TIMEOUT_MS);
    if (result == HAL_OK) result = send(bus, 0, init, sizeof init);
    memset(pixels, 0, sizeof pixels);
    if (result == HAL_OK) result = flush(bus);
    const uint8_t on = 0xaf;
    if (result == HAL_OK) result = send(bus, 0, &on, 1);
    return result;
}

static void line(uint8_t row, const char *text)
{
    for (uint16_t x = 0; *text && x + 5U <= 128U; x += 6U, ++text) {
        const uint8_t *glyph = NULL;
        if (*text >= 'A' && *text <= 'Z') glyph = letters[*text - 'A'];
        if (*text >= '0' && *text <= '9') glyph = digits[*text - '0'];
        if (glyph) memcpy(pixels + row * 128U + x, glyph, 5);
    }
}

HAL_StatusTypeDef OLED_Text(I2C_HandleTypeDef *bus, const char *a, const char *b,
                            const char *c, const char *d)
{
    memset(pixels, 0, sizeof pixels);
    line(0, a);
    line(1, b);
    line(2, c);
    line(3, d);
    return flush(bus);
}

HAL_StatusTypeDef OLED_TestFrame(I2C_HandleTypeDef *bus, uint32_t frame)
{
    memset(pixels, 0, sizeof pixels);
    if (frame < 4U) {
        /* Brief alternating checkerboards exercise every pixel without
         * leaving the entire panel continuously lit.
         */
        for (uint16_t i = 0; i < sizeof pixels; ++i)
            pixels[i] = ((i + frame) & 1U) ? 0x55 : 0xaa;
    } else {
        char counter[] = "FRAME 000000";
        uint32_t n = frame;
        for (int i = 11; i >= 6; --i) {
            counter[i] = (char)('0' + n % 10U);
            n /= 10U;
        }
        line(0, "UPS DISPLAY TEST");
        line(1, "I2C2 3V3 100KHZ");
        line(2, counter);
        line(3, "TEST DATA ONLY");
    }
    return flush(bus);
}
