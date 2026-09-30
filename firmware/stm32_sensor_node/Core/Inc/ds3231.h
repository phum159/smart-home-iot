/*
 * ds3231.h
 *
 *  Created on: 4 ก.ค. 2569
 *      Author: Phum
 */

#ifndef DS3231_H
#define DS3231_H

#include "stm32f1xx_hal.h"

#define DS3231_ADDR  (0x68 << 1)

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t date;
    uint8_t month;
    uint8_t year; // 2 หลัก เช่น 26 = 2026
} DS3231_Time_t;

HAL_StatusTypeDef DS3231_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef DS3231_SetTime(I2C_HandleTypeDef *hi2c,
                                  DS3231_Time_t *t);
HAL_StatusTypeDef DS3231_GetTime(I2C_HandleTypeDef *hi2c,
                                  DS3231_Time_t *t);
void DS3231_ToString(DS3231_Time_t *t, char *buf);

#endif
