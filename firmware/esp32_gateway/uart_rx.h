#ifndef UART_RX_H
#define UART_RX_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ต้องตรงกับที่ STM32 ส่งมาใน UART_TX_SendJSON()
// (PZEM ถอดออกจากระบบแล้ว จึงไม่มีฟิลด์ voltage/current/power/energy)
typedef struct {
    float    temperature;
    float    humidity;
    float    lux;
    uint8_t  motion;            // 1 เมื่อ presence != none
    char     presence[16];      // "none" / "static" / "motion_static"
    uint16_t motion_dist_cm;
    uint16_t static_dist_cm;
    char     timestamp[32];
    float    stm32_temp;
    bool     valid;
} SensorData_t;

bool UART_RX_Parse(String &line, SensorData_t *data);
bool UART_RX_ParseCmd(String &line, char *cmd, size_t cmd_size);

#endif
