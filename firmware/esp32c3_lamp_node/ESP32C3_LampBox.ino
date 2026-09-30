#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> // ใช้สำหรับกำหนดช่องสัญญาณ (channel) ของวิทยุ

#define RELAY_PIN 5

// ── ระยะเวลาระหว่างการส่งข้อมูลอุณหภูมิ (มิลลิวินาที) ──
// ค่าปัจจุบันตั้งไว้สำหรับการทดสอบ ค่าสำหรับใช้งานจริงคือ 60000
#define TELEMETRY_INTERVAL_MS  10000

const char* ssid = "Smart_Home"; // ชื่อเครือข่าย WiFi ใช้เพื่อสแกนหาช่องสัญญาณเท่านั้น ไม่ได้เชื่อมต่อ

typedef struct {
    char cmd[12];
} ESPNow_Cmd_t;

typedef struct {
    float c3_temp;
} NodeData_t;
NodeData_t outData;

// MAC Address ของ ESP32 Gateway ปลายทาง
uint8_t gateway_mac[] = {0xCC, 0x7B, 0x5C, 0x28, 0x39, 0xA0};

// ── ที่อยู่ broadcast สำหรับการวินิจฉัยปัญหา ──
// เฟรม broadcast ไม่มีการตอบรับ (ACK) อุปกรณ์ทุกตัวในระยะสามารถรับได้
// หาก gateway รับ broadcast ได้ แต่ unicast ไม่ได้รับ ACK แสดงว่าปัญหาอยู่ที่การตอบรับ ไม่ใช่ระยะสัญญาณ
// หาก broadcast ไม่ถึงเช่นกัน แสดงว่าสัญญาณไปไม่ถึง (ระยะทาง เสาอากาศ หรือกำลังส่ง)
uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── สถานะของการส่งที่รอผลลัพธ์ ──
// รุ่นก่อนหน้าใช้ตัวแปรสถานะเพียงตัวเดียวและรีเซ็ตตามเวลา ซึ่งทำงานผิดพลาด
// เนื่องจาก unicast ที่ส่งไม่สำเร็จจะถูกส่งซ้ำเกือบหนึ่งวินาทีก่อน callback ทำงาน
// ส่งผลให้ผลของ broadcast ถูกรายงานเป็นผลของ unicast
volatile bool cb_pending      = false;
volatile bool cb_is_broadcast = false;

// ── ตัวนับผลการส่ง ──
uint32_t tx_total = 0;
uint32_t tx_ack   = 0;
uint32_t tx_noack = 0;

// จำนวนครั้งที่ส่งไม่สำเร็จติดต่อกัน เมื่อถึงเกณฑ์จะสแกนหาช่องสัญญาณใหม่
volatile uint8_t consec_noack = 0;
#define RESCAN_AFTER_NOACK  3

int32_t current_channel = 1;

// ประกาศฟังก์ชันล่วงหน้า เนื่องจาก resyncChannel() ถูกเรียกใช้ก่อนตำแหน่งที่นิยาม
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
//  Callback ผลการส่ง
//  ค่าที่ esp_now_send() คืนมามีความหมายเพียงว่า "เข้าคิวสำเร็จ"
//  แม้ gateway ไม่ได้เปิดอยู่ก็ยังได้ผลเป็น OK
//  สถานะการส่งถึงปลายทางจริงทราบได้จาก callback นี้เท่านั้น
// ══════════════════════════════════════════════════════════
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (cb_is_broadcast) {
        // broadcast ไม่มีการตอบรับ สถานะนี้ระบุได้เพียงว่าส่งออกไปแล้ว
        Serial.println("   [BROADCAST] ส่งออกไปแล้ว (ไม่มี ACK) — ตรวจสอบการรับที่ฝั่ง gateway");
        cb_pending = false;
        return;
    }
    if (status == ESP_NOW_SEND_SUCCESS) {
        tx_ack++;
        consec_noack = 0;
        Serial.printf("   [ACK] gateway ตอบรับแล้ว (ack=%lu / noack=%lu)\n",
                      (unsigned long)tx_ack, (unsigned long)tx_noack);
        cb_pending = false;
    } else {
        tx_noack++;
        if (consec_noack < 255) consec_noack++;
        Serial.printf("   [NO-ACK] ส่งออกไปแล้วแต่ไม่ได้รับการตอบรับ (ack=%lu / noack=%lu)\n",
                      (unsigned long)tx_ack, (unsigned long)tx_noack);
        cb_pending = false;
    }
}

// ══════════════════════════════════════════════════════════
//  ส่งข้อมูลและรอผลลัพธ์ก่อนคืนค่า
//  จำเป็นเนื่องจาก unicast ที่ส่งไม่สำเร็จจะถูกส่งซ้ำนานเกือบ 1 วินาที
//  หากส่งครั้งถัดไปทันที ผลของการส่งทั้งสองครั้งจะปะปนกัน
// ══════════════════════════════════════════════════════════
static void sendAndWait(uint8_t *dest, bool is_broadcast) {
    cb_is_broadcast = is_broadcast;
    cb_pending      = true;

    esp_err_t r = esp_now_send(dest, (uint8_t *) &outData, sizeof(outData));
    if (r != ESP_OK) {
        Serial.printf("   เพิ่มเข้าคิวไม่สำเร็จ: %s\n", esp_err_to_name(r));
        cb_pending = false;
        return;
    }

    uint32_t t0 = millis();
    while (cb_pending && (millis() - t0) < 3000) delay(5);

    if (cb_pending) {
        Serial.println("   (ไม่ได้รับ callback ภายใน 3 วินาที)");
        cb_pending = false;
    }
}

// ══════════════════════════════════════════════════════════
//  สแกนหาช่องสัญญาณใหม่และย้ายไปใช้ช่องนั้น
//  รุ่นก่อนหน้าสแกนเพียงครั้งเดียวตอนเริ่มทำงานแล้วใช้ช่องนั้นตลอด
//  หากเราเตอร์ปิดอยู่ขณะเริ่มทำงาน อุปกรณ์จะใช้ช่อง 1 และไม่สามารถ
//  สื่อสารกับ gateway ได้อีกจนกว่าจะรีสตาร์ท
// ══════════════════════════════════════════════════════════
void resyncChannel(void) {
    Serial.println(">> ส่งไม่สำเร็จติดต่อกันหลายครั้ง กำลังสแกนหาช่องสัญญาณใหม่...");

    int32_t ch = getWiFiChannel(ssid);
    if (ch == 0) {
        Serial.println(">> ไม่พบเครือข่าย Smart_Home (เราเตอร์อาจยังไม่พร้อม) จะลองใหม่ภายหลัง");
        return;
    }

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    // peer ผูกอยู่กับช่องสัญญาณเดิม จึงต้องลบและเพิ่มใหม่ด้วยช่องที่ถูกต้อง
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
        Serial.printf(">> ย้ายจากช่อง %d ไปยังช่อง %d แล้ว\n", current_channel, ch);
    } else {
        Serial.printf(">> ยังคงอยู่ช่อง %d สาเหตุไม่น่าเกิดจากช่องสัญญาณ\n", ch);
    }
    current_channel = ch;
    consec_noack = 0;
}

static void printMac(const char *label, const uint8_t *mac) {
    Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n",
                  label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ── สแกนหาช่องสัญญาณของเราเตอร์ โดยไม่ต้องเชื่อมต่อเครือข่าย ──
int32_t getWiFiChannel(const char *target_ssid) {
    if (int32_t n = WiFi.scanNetworks()) {
        for (uint8_t i = 0; i < n; i++) {
            if (!strcmp(target_ssid, WiFi.SSID(i).c_str())) {
                return WiFi.channel(i);
            }
        }
    }
    return 0; // คืนค่า 0 หากไม่พบเครือข่ายที่ระบุ
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);   // ESP-NOW ต้องให้วิทยุทำงานตลอดเวลา เช่นเดียวกับฝั่ง gateway

    Serial.println("Scanning for Gateway's WiFi channel...");
    int32_t channel = getWiFiChannel(ssid);

    // กำหนดช่องสัญญาณของวิทยุให้ตรงกับ gateway
    if (channel != 0) {
        Serial.printf("Found %s on Channel %d. Synced!\n", ssid, channel);
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
    } else {
        Serial.println("WiFi not found! Using default channel 1.");
        channel = 1; // ค่าสำรองในกรณีที่เราเตอร์ปิดอยู่
    }

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);   // รับผลการส่งจริงจากปลายทาง

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, gateway_mac, 6);
    peerInfo.channel = channel; // ใช้ช่องสัญญาณที่สแกนพบ
    peerInfo.encrypt = false;

    esp_err_t addResult = esp_now_add_peer(&peerInfo);
    Serial.printf("esp_now_add_peer (gateway): %s\n",
                  addResult == ESP_OK ? "OK" : esp_err_to_name(addResult));

    // peer สำหรับ broadcast ใช้เพื่อการวินิจฉัยเท่านั้น
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, broadcast_mac, 6);
    bcast.channel = channel;
    bcast.encrypt = false;
    esp_err_t bResult = esp_now_add_peer(&bcast);
    Serial.printf("esp_now_add_peer (broadcast): %s\n",
                  bResult == ESP_OK ? "OK" : esp_err_to_name(bResult));

    // ── สรุปข้อมูลสำหรับการตรวจสอบ ──
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

    // ── อ่านช่องสัญญาณจากวิทยุโดยตรง แทนค่าที่บันทึกไว้ตอนสแกน ──
    // หาก esp_wifi_set_channel() ไม่มีผล ค่าทั้งสองจะไม่ตรงกัน
    uint8_t prim = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&prim, &sec);

    Serial.printf("[%lu] กำลังส่ง c3_temp = %.2f C (%d ไบต์) | ช่องสัญญาณปัจจุบัน %u (ที่บันทึกไว้ %d)\n",
                  (unsigned long)tx_total, t, (int)sizeof(outData),
                  prim, (int)current_channel);

    // ── รอบที่ 1: ส่งตรงไปยัง gateway (มีการตอบรับ) ──
    sendAndWait(gateway_mac, false);

    // ── รอบที่ 2: ส่ง broadcast เพื่อตรวจสอบว่าสัญญาณไปถึงหรือไม่ ──
    sendAndWait(broadcast_mac, true);

    if (consec_noack >= RESCAN_AFTER_NOACK) {
        resyncChannel();
    }

    delay(TELEMETRY_INTERVAL_MS);
}
