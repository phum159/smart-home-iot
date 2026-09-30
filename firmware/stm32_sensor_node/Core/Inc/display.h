#ifndef DISPLAY_H
#define DISPLAY_H

#include "stm32f1xx_hal.h"
#include "uart_tx.h"
#include "ds3231.h"

void Display_Update(SensorData_t *data,
                    DS3231_Time_t *rtc,
                    uint8_t light_on,
                    uint8_t wifi_ok,
                    uint8_t i2c_error_count);

#endif
