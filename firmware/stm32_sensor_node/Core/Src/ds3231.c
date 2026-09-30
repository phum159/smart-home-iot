#include "ds3231.h"
#include <stdio.h>
// BCD ↔ Decimal
static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

HAL_StatusTypeDef DS3231_Init(I2C_HandleTypeDef *hi2c) {
    // เช็คว่าคุยได้ไหม
    return HAL_I2C_IsDeviceReady(hi2c, DS3231_ADDR, 2, 100);
}

HAL_StatusTypeDef DS3231_SetTime(I2C_HandleTypeDef *hi2c,
                                  DS3231_Time_t *t) {
    uint8_t buf[8];
    buf[0] = 0x00;              // register start
    buf[1] = dec2bcd(t->sec);
    buf[2] = dec2bcd(t->min);
    buf[3] = dec2bcd(t->hour);
    buf[4] = 0x01;              // day of week (ไม่ใช้)
    buf[5] = dec2bcd(t->date);
    buf[6] = dec2bcd(t->month);
    buf[7] = dec2bcd(t->year);

    return HAL_I2C_Master_Transmit(hi2c, DS3231_ADDR,
                                    buf, 8, 100);
}

HAL_StatusTypeDef DS3231_GetTime(I2C_HandleTypeDef *hi2c,
                                  DS3231_Time_t *t) {
    uint8_t reg = 0x00;
    uint8_t buf[7];

    if (HAL_I2C_Master_Transmit(hi2c, DS3231_ADDR,
                                 &reg, 1, 100) != HAL_OK)
        return HAL_ERROR;

    if (HAL_I2C_Master_Receive(hi2c, DS3231_ADDR,
                                buf, 7, 100) != HAL_OK)
        return HAL_ERROR;

    t->sec   = bcd2dec(buf[0] & 0x7F);
    t->min   = bcd2dec(buf[1]);
    t->hour  = bcd2dec(buf[2] & 0x3F);
    t->date  = bcd2dec(buf[4]);
    t->month = bcd2dec(buf[5] & 0x1F);
    t->year  = bcd2dec(buf[6]);

    return HAL_OK;
}

void DS3231_ToString(DS3231_Time_t *t, char *buf) {
    // format: "2026-07-04 19:05:30"
    sprintf(buf, "20%02d-%02d-%02d %02d:%02d:%02d",
            t->year, t->month, t->date,
            t->hour, t->min, t->sec);
}
