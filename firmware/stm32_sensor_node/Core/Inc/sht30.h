#ifndef SHT30_H
#define SHT30_H

#include "stm32f1xx_hal.h"

#define SHT30_ADDR        (0x44 << 1)

// Commands
#define SHT30_MEAS_HIGHREP 0x2400  // High repeatability

HAL_StatusTypeDef SHT30_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef SHT30_Read(I2C_HandleTypeDef *hi2c,
                               float *temp,
                               float *hum);
#endif
