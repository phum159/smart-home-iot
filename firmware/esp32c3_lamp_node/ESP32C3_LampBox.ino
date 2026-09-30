#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> // ต้อง include ไลบรารีนี้เพิ่มสำหรับการจูนคลื่นวิทยุ

#define RELAY_PIN 5

// ── โหมด debug: ส่งถี่ขึ้นเพื่อไม่ต้องรอนาน ──
// เสร็จการตรวจแล้วเปลี่ยนกลับเป็น 60000 ด้วย
#define TELEMETRY_INTERVAL_MS  10000

const char* ssid = "Smart_Home"; // ใส่แค่ชื่อ WiFi เพื่อใช้เป็นเป้าหมายในการสแกนหา Channel

typedef struct {
    char cmd[12];
} ESPNow_Cmd_t;

typedef struct {
    float c3_temp;
} NodeData_t;
NodeData_t outData;

// MAC Address ของ ESP32 Gateway
uint8_t gateway_mac[] = {0xCC, 0x7B, 0x5C, 0x28, 0x39, 0xA0};

// ── ที่อยู่ broadcast สำหรับการทดสอบ ──
// เฟรม broadcast ไม่มีการ ACK ตอบกลับ ใครอยู่ในระยะก็รับได้หมด
// ถ้า gateway รับ broadcast ได้แต่ unicast ไม่ ACK = ปัญหาอยู่ที่ชั้น ACK ไม่ใช่ระยะ
// ถ้า broadcast ก็ไม่ถึง = คลื่นไปไม่ถึงกันจริงๆ (ระยะ/เสาอากาศ/กำลังส่ง)
uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── สถานะการส่งที่กำลังรอผลอยู่ ──
// เดิมใช้ธงตัวเดียวแล้วรีเซ็ตตามเวลา ซึ่งพลาด เพราะ unicast ที่ส่งไม่ถึง
// จะลองซ้ำเกือบวินาทีกว่า callback จะกลับมา พอถึงตอนนั้นธงถูกเปลี่ยนไปแล้ว
// ทำให้ผลของ broadcast ถูกรายงานเป็นผลของ unicast
volatile bool cb_pending      = false;
volatile bool cb_is_broadcast = false;

// ── ตัวนับผลการส่งจริง ──
uint32_t tx_total = 0;
uint32_t tx_ack   = 0;
uint32_t tx_noack = 0;

// ส่งไม่ถึงติดกันกี่ครั้งแล้ว ถ้าถึงเกณฑ์จะสแกนหา channel ใหม่
volatile uint8_t consec_noack = 0;
#define RESCAN_AFTER_NOACK  3

int32_t current_channel = 1;

// ประกาศล่วงหน้า เพราะ resyncChannel() เรียกใช้ก่อนถึงบรรทัดที่นิยามจริง
int32_t getWiFiChannel(const char *target_ssid);
void    resyncChannel(void);

void OnDataRecv(const esp_now_recv_info *esp_now_info, const uint8_t *incomingData, int len) {
    ESPNow_Cmd_t pkt;
    memcpy(&pkt, incomingData, sizeof(pkt));

    Serial.printf("Received CMD: %s\n", pkt.cmd);

    if (strcmp(pkt.cmd, "LIGHT_ON") == 0) {
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("-> RELAY IS ON");
    }
    else if (strcmp(pkt.cmd, "LIGHT_OFF") == 0) {
        digitalWrite(RELAY_PIN, LOW);
        Serial.println("-> RELAY IS OFF");
    }
}

// ══════════════════════════════════════════════════════════
//  ตัวนี้แหละที่บอก "ความจริง"
//  เดิม C3 ดูแค่ค่าที่ esp_now_send() คืนมา ซึ่งแปลว่า
//  "ใส่คิวสำเร็จ" เท่านั้น ถอดปลั๊ก gateway ทิ้งก็ยังขึ้น OK
//  สถานะการส่งถึงจริงมาทางนี้ทางเดียว
// ══════════════════════════════════════════════════════════
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (cb_is_broadcast) {
        // broadcast ไม่มี ACK อยู่แล้ว สถานะตรงนี้บอกได้แค่ว่ายิงออกไปแล้ว
        Serial.println("   [BROADCAST] ยิงออกไปแล้ว (ไม่มี ACK ให้ดู) — ไปดูฝั่ง gateway ว่ารับได้ไหม");
        cb_pending = false;
        return;
    }
    if (status == ESP_NOW_SEND_SUCCESS) {
        tx_ack++;
        consec_noack = 0;
        Serial.printf("   [ACK] gateway ตอบรับแล้ว  (ack=%lu / noack=%lu)\n",
                      (unsigned long)tx_ack, (unsigned long)tx_noack);
        cb_pending = false;
    } else {
        tx_noack++;
        if (consec_noack < 255) consec_noack++;
        Serial.printf("   [NO-ACK] ยิงออกไปแล้วแต่ไม่มีใครรับ  (ack=%lu / noack=%lu)\n",
                      (unsigned long)tx_ack, (unsigned long)tx_noack);
        cb_pending = false;
    }
}

// ══════════════════════════════════════════════════════════
//  ส่งแล้วรอผลให้จบก่อนคืนค่า
//  จำเป็นเพราะ unicast ที่ส่งไม่ถึงจะลองซ้ำเกือบ 1 วินาที
//  ถ้าไม่รอ แล้วยิงตัวถัดไปเลย ผลของสองการส่งจะปนกันจนอ่านผิด
// ══════════════════════════════════════════════════════════
static void sendAndWait(uint8_t *dest, bool is_broadcast) {
    cb_is_broadcast = is_broadcast;
    cb_pending      = true;

    esp_err_t r = esp_now_send(dest, (uint8_t *) &outData, sizeof(outData));
    if (r != ESP_OK) {
        Serial.printf("   ใส่คิวไม่สำเร็จ: %s\n", esp_err_to_name(r));
        cb_pending = false;
        return;
    }

    uint32_t t0 = millis();
    while (cb_pending && (millis() - t0) < 3000) delay(5);

    if (cb_pending) {
        Serial.println("   (callback ไม่กลับมาเลยภายใน 3 วิ)");
        cb_pending = false;
    }
}

// ══════════════════════════════════════════════════════════
//  สแกนหา channel ใหม่แล้วย้ายตาม
//  จำเป็นเพราะเดิม C3 สแกนครั้งเดียวตอนบูตแล้วล็อกค้างตลอดไป
//  ถ้าตอนบูตเร้าเตอร์ดับอยู่ มันจะตกไป channel 1 แล้วไม่มีทางกลับมาเจอ
//  gateway อีกเลยจนกว่าจะถอดปลั๊กเสียบใหม่
// ══════════════════════════════════════════════════════════
void resyncChannel(void) {
    Serial.println(">> ส่งไม่ถึงติดกันหลายครั้ง กำลังสแกนหา channel ใหม่...");

    int32_t ch = getWiFiChannel(ssid);
    if (ch == 0) {
        Serial.println(">> ยังไม่เจอ Smart_Home (เร้าเตอร์อาจยังไม่กลับมา) เดี๋ยวลองใหม่");
        return;
    }

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // peer ผูกกับ channel เดิมอยู่ ต้องลบแล้วเพิ่มใหม่ให้ตรงกัน
    esp_now_del_peer(gateway_mac);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, gateway_mac, 6);
    peerInfo.channel = ch;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    esp_now_del_peer(broadcast_mac);
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, broadcast_mac, 6);
    bcast.channel = ch;
    bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    if (ch != current_channel) {
        Serial.printf(">> ย้ายจาก channel %d ไป %d แล้ว\n", current_channel, ch);
    } else {
        Serial.printf(">> ยังอยู่ channel %d เหมือนเดิม ปัญหาน่าจะไม่ใช่เรื่อง channel\n", ch);
    }
    current_channel = ch;
    consec_noack = 0;
}

static void printMac(const char *label, const uint8_t *mac) {
    Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n",
                  label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ── ฟังก์ชันสแกนหา Channel ของเร้าเตอร์โดยไม่ต้องต่อเน็ต ──
int32_t getWiFiChannel(const char *target_ssid) {
    if (int32_t n = WiFi.scanNetworks()) {
        for (uint8_t i = 0; i < n; i++) {
            if (!strcmp(target_ssid, WiFi.SSID(i).c_str())) {
                return WiFi.channel(i);
            }
        }
    }
    return 0; // คืนค่า 0 หากไม่เจอชื่อ WiFi นี้
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);   // ESP-NOW ต้องการวิทยุตื่นตลอด เหมือนที่ตั้งไว้ฝั่ง gateway

    Serial.println("Scanning for Gateway's WiFi channel...");
    int32_t channel = getWiFiChannel(ssid);

    // บังคับเปลี่ยนคลื่นวิทยุ (Channel) ให้ตรงกับ Gateway
    if (channel != 0) {
        Serial.printf("Found %s on Channel %d. Synced!\n", ssid, channel);
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
    } else {
        Serial.println("WiFi not found! Using default channel 1.");
        channel = 1; // Fallback หากเร้าเตอร์ปิดอยู่
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);   // ← ของใหม่ ไม่เคยมีมาก่อน

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, gateway_mac, 6);
    peerInfo.channel = channel; // ใช้ Channel ที่สแกนเจอ
    peerInfo.encrypt = false;

    esp_err_t addResult = esp_now_add_peer(&peerInfo);
    Serial.printf("esp_now_add_peer (gateway): %s\n",
                  addResult == ESP_OK ? "OK" : esp_err_to_name(addResult));

    // peer สำหรับ broadcast ใช้ทดสอบอย่างเดียว
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, broadcast_mac, 6);
    bcast.channel = channel;
    bcast.encrypt = false;
    esp_err_t bResult = esp_now_add_peer(&bcast);
    Serial.printf("esp_now_add_peer (broadcast): %s\n",
                  bResult == ESP_OK ? "OK" : esp_err_to_name(bResult));

    // ── สรุปข้อมูลที่ต้องใช้ตรวจสอบ ──
    Serial.println("---------------- ข้อมูลตรวจสอบ ----------------");
    Serial.printf("C3 MAC (ตัวเอง)   : %s\n", WiFi.macAddress().c_str());
    printMac("Gateway MAC (เป้าหมาย):", gateway_mac);
    Serial.printf("Channel ที่ใช้     : %d\n", channel);
    Serial.println("-----------------------------------------------");
    Serial.println("ESP-NOW Ready (Node)");

    current_channel = channel;
}

void loop() {
    float t = temperatureRead();
    outData.c3_temp = t;
    tx_total++;

    // ── อ่าน channel จากตัววิทยุจริงๆ ไม่ใช่ค่าที่จำไว้ตอนสแกน ──
    // ถ้า esp_wifi_set_channel() ไม่เป็นผล ตัวเลขสองอันนี้จะไม่ตรงกัน
    uint8_t prim = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&prim, &sec);

    Serial.printf("[%lu] กำลังส่ง c3_temp = %.2f C (%d ไบต์) | วิทยุอยู่ channel %u (จำไว้ว่า %d)\n",
                  (unsigned long)tx_total, t, (int)sizeof(outData),
                  prim, (int)current_channel);

    // ── รอบที่ 1: ยิงตรงไปที่ gateway (มี ACK จริง) ──
    sendAndWait(gateway_mac, false);

    // ── รอบที่ 2: ยิง broadcast ทดสอบว่าคลื่นไปถึงกันไหม ──
    sendAndWait(broadcast_mac, true);

    if (consec_noack >= RESCAN_AFTER_NOACK) {
        resyncChannel();
    }

    delay(TELEMETRY_INTERVAL_MS);
}
