#ifndef BH1750_H
#define BH1750_H

#include "stm32f1xx_hal.h"

#define BH1750_ADDR      (0x23 << 1)
#define BH1750_POWER_ON  0x01
#define BH1750_CONT_HRES 0x10

HAL_StatusTypeDef BH1750_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef BH1750_Read(I2C_HandleTypeDef *hi2c,
                               float *lux);
#endif
