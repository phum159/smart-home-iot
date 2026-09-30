#include "automation.h"

bool Automation_ShouldTurnOn(SensorData_t *data,
                              AutoConfig_t *cfg,
                              int current_hour) {
    if (!cfg->auto_mode)   return false;
    if (!data->motion)     return false;
    if (data->lux >= cfg->lux_threshold) return false;
    if (current_hour < cfg->auto_hour)   return false;
    return true;
}

bool Automation_ShouldTurnOff(uint32_t last_motion_ms,
                               AutoConfig_t *cfg) {
    return (millis() - last_motion_ms)
           >= cfg->no_motion_timeout_ms;
}