#ifndef ESPNOW_TX_H
#define ESPNOW_TX_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

typedef struct {
    char cmd[12]; // "LIGHT_ON" / "LIGHT_OFF"
} ESPNow_Cmd_t;

void ESPNOW_Init(void);
void ESPNOW_SendCmd(const char *cmd);

#endif