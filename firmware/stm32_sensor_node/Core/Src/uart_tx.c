#include "uart_tx.h"
#include "ld2412.h"
#include <stdio.h>
#include <string.h>

void UART_TX_SendJSON(UART_HandleTypeDef *huart,
                      SensorData_t *data) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{"
        "\"temp\":%.1f,"
        "\"hum\":%.1f,"
        "\"lux\":%.0f,"
        "\"motion\":%d,"
        "\"presence\":\"%s\","
        "\"m_dist\":%d,"
        "\"s_dist\":%d,"
        "\"ts\":\"%s\","
    	"\"stm32_temp\":%.2f""}\n",
        data->temperature,
        data->humidity,
        data->lux,
        data->motion,
        LD2412_PresenceToString(data->presence_state),
        data->motion_dist_cm,
        data->static_dist_cm,
        data->timestamp,
		data->stm32_temp
    );

    HAL_UART_Transmit(huart,
                      (uint8_t *)buf,
                      strlen(buf),
                      200);
}

void UART_TX_SendCmd(UART_HandleTypeDef *huart,
                     const char *cmd) {
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"%s\"}\n", cmd);
    HAL_UART_Transmit(huart,
                      (uint8_t *)buf,
                      strlen(buf), 100);
}

void UART_TX_SendRadarDebug(UART_HandleTypeDef *huart) {
    uint32_t evt, ok, rst;
    uint16_t last_size;
    uint8_t  raw[32];

    LD2412_GetDebug(&evt, &ok, &rst, &last_size, raw, sizeof(raw));

    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "RAW:evt=%lu ok=%lu rst=%lu size=%u data=",
                     (unsigned long)evt, (unsigned long)ok,
                     (unsigned long)rst, last_size);

    uint16_t show = (last_size < sizeof(raw)) ? last_size : sizeof(raw);
    for (uint16_t i = 0; i < show && n < (int)sizeof(buf) - 4; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "%02X", raw[i]);
    }
    n += snprintf(buf + n, sizeof(buf) - n, "\n");
    (void)n;

    HAL_UART_Transmit(huart, (uint8_t *)buf, strlen(buf), 200);
}
