#ifndef AUTOMATION_H
#define AUTOMATION_H

#include "uart_rx.h"

typedef struct {
    bool  auto_mode;
    int   auto_hour;        // เวลาเริ่ม auto (ชั่วโมง)
    float lux_threshold;    // ค่าแสงที่จะเปิดไฟ
    uint32_t no_motion_timeout_ms; // ms ก่อนปิดไฟ
} AutoConfig_t;

bool Automation_ShouldTurnOn(SensorData_t *data,
                              AutoConfig_t *cfg,
                              int current_hour);

bool Automation_ShouldTurnOff(uint32_t last_motion_ms,
                               AutoConfig_t *cfg);

#endif