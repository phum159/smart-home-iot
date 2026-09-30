#ifndef UART_TX_H
#define UART_TX_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct {
    float    temperature;
    float    humidity;
    float    lux;
    uint8_t  motion;          // derived: 1 if presence_state != LD2412_STATE_NONE
    uint8_t  presence_state;  // raw LD2412 target-state byte (0x00/0x02/0x03)
    uint16_t motion_dist_cm;
    uint16_t static_dist_cm;
    char     timestamp[32];
    float    stm32_temp;
} SensorData_t;

void UART_TX_SendJSON(UART_HandleTypeDef *huart,
                      SensorData_t *data);

void UART_TX_SendCmd(UART_HandleTypeDef *huart,
                     const char *cmd);

// ส่งไบต์ดิบของเรดาร์ไปให้ ESP32 พิมพ์ออก Serial Monitor
// ESP32 จะเห็นบรรทัดขึ้นต้นด้วย "RAW:" แล้วส่งต่อออกจอโดยไม่ parse
void UART_TX_SendRadarDebug(UART_HandleTypeDef *huart);

#endif
