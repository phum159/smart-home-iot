#include "uart_rx.h"

bool UART_RX_Parse(String &line, SensorData_t *data) {
    StaticJsonDocument<320> doc;
    DeserializationError err = deserializeJson(doc, line);

    if (err) return false;
    if (!doc.containsKey("temp")) return false;   // ไม่ใช่เฟรมเซนเซอร์

    data->temperature    = doc["temp"]       | 0.0f;
    data->humidity       = doc["hum"]        | 0.0f;
    data->lux            = doc["lux"]        | 0.0f;
    data->motion         = doc["motion"]     | 0;
    data->motion_dist_cm = doc["m_dist"]     | 0;
    data->static_dist_cm = doc["s_dist"]     | 0;
    data->stm32_temp     = doc["stm32_temp"] | 0.0f;

    const char *pres = doc["presence"] | "none";
    strncpy(data->presence, pres, sizeof(data->presence) - 1);
    data->presence[sizeof(data->presence) - 1] = '\0';

    const char *ts = doc["ts"] | "";
    strncpy(data->timestamp, ts, sizeof(data->timestamp) - 1);
    data->timestamp[sizeof(data->timestamp) - 1] = '\0';

    data->valid = true;
    return true;
}

bool UART_RX_ParseCmd(String &line, char *cmd, size_t cmd_size) {
    // 1. ข้อความเพียวๆ จากปุ่มบน STM32
    if (line == "LIGHT_ON" || line == "LIGHT_OFF") {
        strncpy(cmd, line.c_str(), cmd_size - 1);
        cmd[cmd_size - 1] = '\0';
        return true;
    }

    // 2. แบบ JSON {"cmd":"LIGHT_ON"} ที่ UART_TX_SendCmd() ส่งมา
    StaticJsonDocument<64> doc;
    if (deserializeJson(doc, line)) return false;
    if (!doc.containsKey("cmd")) return false;

    const char *c = doc["cmd"];
    if (!c) return false;
    strncpy(cmd, c, cmd_size - 1);
    cmd[cmd_size - 1] = '\0';
    return true;
}
