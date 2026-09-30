#include "espnow_tx.h"

// MAC Address ของ ESP32-C3 (กล่อง 3)
uint8_t c3_mac[] = {0x08, 0x92, 0x72,
                    0x23, 0xBE, 0x48};

static esp_now_peer_info_t peer;

void onSent(const wifi_tx_info_t *tx_info,
            esp_now_send_status_t status) {
    Serial.print("ESP-NOW: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS
                   ? "OK" : "FAIL");
}

void ESPNOW_Init(void) {

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_send_cb(onSent);

    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, c3_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    Serial.println("ESP-NOW ready");
}

void ESPNOW_SendCmd(const char *cmd) {
    ESPNow_Cmd_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    strncpy(pkt.cmd, cmd, sizeof(pkt.cmd) - 1);

    esp_now_send(c3_mac,
                 (uint8_t *)&pkt,
                 sizeof(pkt));
}