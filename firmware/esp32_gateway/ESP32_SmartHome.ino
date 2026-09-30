#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "secrets.h"
#include "uart_rx.h"
#include "espnow_tx.h"
#include "mqtt_client.h"

// ══════════════════════════════════════════════════════════
//  ESP32 Gateway (Box 1)
//   - รับ JSON เซนเซอร์จาก STM32 ผ่าน UART2
//   - ส่งขึ้น Mosquitto ผ่าน MQTT (HA auto-discovery)
//   - สั่งรีเลย์ที่ Box3 ผ่าน ESP-NOW
// ══════════════════════════════════════════════════════════

#define RXD2 16
#define TXD2 17

SensorData_t g_sensor = {0};

bool light_on      = false;
bool prev_light_on = false;

// บันทึกไว้ว่าครั้งล่าสุดใครเป็นคนสั่งไฟ ส่งขึ้นไปกับ snapshot ด้วย
// เผื่อวันหน้าทำ FastAPI แล้วอยากรู้ว่าไฟติดเพราะ HA หรือเพราะคนกดปุ่ม
const char *light_src = "boot";

float current_c3_temp = 0.0f;
bool  c3_seen         = false;
volatile bool c3_new  = false;   // มีค่าใหม่รอ publish
typedef struct { float c3_temp; } NodeData_t;

unsigned long last_wifi_check = 0;
bool wifi_was_connected = false;

// ══════════════════════════════════════════════════════════
// ── ESP-NOW ──
// ══════════════════════════════════════════════════════════
void OnNodeDataRecv(const esp_now_recv_info *esp_now_info,
                    const uint8_t *incomingData, int len) {
    if (len == sizeof(NodeData_t)) {
        NodeData_t incoming;
        memcpy(&incoming, incomingData, sizeof(incoming));
        current_c3_temp = incoming.c3_temp;
        c3_seen = true;
        c3_new  = true;
        Serial.printf("ESP-NOW RX: C3 temp = %.1f C\n", current_c3_temp);
    } else {
        Serial.printf("ESP-NOW RX: ขนาดไม่ตรง (%d ไบต์ ควรเป็น %d)\n",
                      len, (int)sizeof(NodeData_t));
    }
}

// ══════════════════════════════════════════════════════════
// ── จุดเดียวที่เปลี่ยนสถานะไฟได้ ──
// ══════════════════════════════════════════════════════════
void applyLight(bool on, const char *src) {
    light_on  = on;
    light_src = src;
    ESPNOW_SendCmd(on ? "LIGHT_ON" : "LIGHT_OFF");
}

// ══════════════════════════════════════════════════════════
// ── Offline buffer: เก็บ snapshot ตอน MQTT ล่ม แล้ว replay ทีหลัง ──
// ══════════════════════════════════════════════════════════
#define OFFLINE_FILE  "/offline.jsonl"
#define OFFLINE_MAX   200000UL      // ~200 KB กันพาร์ทิชันเต็ม

void buildSnapshot(char *out, size_t out_size) {
    StaticJsonDocument<384> doc;
    doc["ts"]         = g_sensor.timestamp;
    doc["temp"]       = g_sensor.temperature;
    doc["hum"]        = g_sensor.humidity;
    doc["lux"]        = g_sensor.lux;
    doc["motion"]     = g_sensor.motion;
    doc["presence"]   = g_sensor.presence;
    doc["m_dist"]     = g_sensor.motion_dist_cm;
    doc["s_dist"]     = g_sensor.static_dist_cm;
    doc["light_on"]   = light_on;
    doc["light_src"]  = light_src;
    doc["stm32_temp"] = g_sensor.stm32_temp;
    doc["c3_temp"]    = current_c3_temp;
    doc["esp32_temp"] = temperatureRead();
    serializeJson(doc, out, out_size);
}

void saveOffline(const char *payload) {
    File f = LittleFS.open(OFFLINE_FILE, "a");
    if (!f) return;
    if (f.size() < OFFLINE_MAX) f.println(payload);
    f.close();
}

void replayOffline(void) {
    if (!LittleFS.exists(OFFLINE_FILE)) return;

    File f = LittleFS.open(OFFLINE_FILE, "r");
    if (!f) return;

    bool all_ok = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (!MQTT_Pub(T_SNAPSHOT, line.c_str(), false)) { all_ok = false; break; }
        delay(20);
    }
    f.close();

    if (all_ok) {
        LittleFS.remove(OFFLINE_FILE);
        Serial.println("Offline buffer replayed");
    }
}

// ══════════════════════════════════════════════════════════
// ── publish ──
// ══════════════════════════════════════════════════════════
void publishSensors(void) {
    MQTT_PubFloat(T_TEMP,       g_sensor.temperature, 1);
    MQTT_PubFloat(T_HUM,        g_sensor.humidity,    1);
    MQTT_PubFloat(T_LUX,        g_sensor.lux,         0);
    MQTT_PubFloat(T_STM32_TEMP, g_sensor.stm32_temp,  1);
    MQTT_PubFloat(T_ESP32_TEMP, temperatureRead(),    1);

    MQTT_Pub(T_PRESENCE, g_sensor.motion ? "ON" : "OFF");
    MQTT_Pub(T_LD_STATE, g_sensor.presence);
    MQTT_PubInt(T_M_DIST, g_sensor.motion_dist_cm);
    MQTT_PubInt(T_S_DIST, g_sensor.static_dist_cm);

    if (c3_seen) MQTT_PubFloat(T_C3_TEMP, current_c3_temp, 1);
}

void publishLightState(void) {
    const char *s = light_on ? "ON" : "OFF";
    MQTT_Pub(T_LIGHT_STATE, s);
    MQTT_Pub(T_RELAY_STATE, s);
}

void publishSnapshot(void) {
    char snap[384];
    buildSnapshot(snap, sizeof(snap));
    if (!MQTT_Pub(T_SNAPSHOT, snap, false)) saveOffline(snap);
}

// ══════════════════════════════════════════════════════════
// ── คำสั่งที่มาจาก MQTT (HA กดสวิตช์) ──
// ══════════════════════════════════════════════════════════
void onMqttCommand(const char *topic, const char *payload) {
    // รับได้ทั้ง home/box3/relay/set และ home/box1/light/set (ผลลัพธ์เหมือนกัน)
    if (strcmp(topic, T_RELAY_SET) != 0 && strcmp(topic, T_LIGHT_SET) != 0) return;

    if (strcmp(payload, "ON") == 0)       applyLight(true,  "mqtt");
    else if (strcmp(payload, "OFF") == 0) applyLight(false, "mqtt");
    else return;

    // ถ้าสถานะเปลี่ยนจริง ปล่อยให้ข้อ 5 ใน loop() เป็นคนแจ้ง STM32 + publish
    // (STM32 เข้าใจแค่ STATE:ON / STATE:OFF ไม่ใช่ LIGHT_ON)
    // ถ้าไม่เปลี่ยน ก็ยืนยันสถานะกลับไปให้ HA ไม่งั้นสวิตช์ใน HA จะค้าง
    if (light_on == prev_light_on) publishLightState();
}

// ══════════════════════════════════════════════════════════
// ── Setup ──
// ══════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);            // ESP-NOW ต้องการวิทยุตื่นตลอด
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    ESPNOW_Init();
    esp_now_register_recv_cb(OnNodeDataRecv);

    MQTT_Init(onMqttCommand);

    // ── ตรวจว่า MAC ที่ C3 ยิงกลับมาหา ตรงกับ MAC จริงของบอร์ดนี้หรือเปล่า ──
    // ถ้าไม่ตรง จะเกิดอาการ "สั่งไฟได้ แต่ไม่เคยได้ค่าอุณหภูมิ C3 กลับมา"
    Serial.print("Gateway STA MAC : ");
    Serial.println(WiFi.macAddress());
    Serial.println("C3 ยิงกลับมาที่: CC:7B:5C:28:39:A0  <-- ต้องตรงกับบรรทัดบน");

    Serial.println("ESP32 Gateway ready (MQTT)");
}

// ══════════════════════════════════════════════════════════
// ── Loop ──
// ══════════════════════════════════════════════════════════
void loop() {
    unsigned long now = millis();

    // ── 1. WiFi auto-reconnect ──
    bool wifi_now = (WiFi.status() == WL_CONNECTED);
    if (!wifi_now && (now - last_wifi_check > 10000)) {
        last_wifi_check = now;
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        Serial.println("Reconnecting WiFi...");
    }
    if (wifi_now != wifi_was_connected) {
        wifi_was_connected = wifi_now;
        Serial2.print(wifi_now ? "WIFI:ON\n" : "WIFI:OFF\n");
    }

    // ── รายงาน channel ที่ใช้จริง ──
    // ESP-NOW จะคุยกันได้ก็ต่อเมื่อทั้งสองฝั่งอยู่ channel เดียวกัน
    // C3 ล็อก channel จากการสแกนครั้งเดียวตอนบูต ส่วน gateway ใช้ channel ของ AP
    // ถ้าเร้าเตอร์เปลี่ยน channel (เช่นหลังไฟดับ) สองฝั่งจะหลุดจากกันทันที
    if (wifi_now) {
        static uint8_t last_ch = 0;
        uint8_t ch = WiFi.channel();
        if (ch != last_ch) {
            last_ch = ch;
            Serial.printf("WiFi: ต่อแล้ว channel=%d  AP=%s  RSSI=%d\n",
                          ch, WiFi.BSSIDstr().c_str(), WiFi.RSSI());
            Serial.println("     ^ ตัวเลข channel นี้ต้องตรงกับที่ C3 ล็อกไว้");
        }
    }

    // ── 2. MQTT (reconnect จัดการอยู่ข้างใน) ──
    bool was_connected = MQTT_Connected();
    MQTT_Loop();
    if (!was_connected && MQTT_Connected()) {
        // เพิ่งกลับมาต่อได้ — ส่งสถานะปัจจุบัน แล้วไล่ข้อมูลที่ค้างไว้
        publishSensors();
        publishLightState();
        replayOffline();
    }

    // ── 2.5 ค่าจาก C3 ส่ง publish ทันทีที่ได้รับ ──
    //     ไม่ผูกกับเฟรมของ STM32 ไม่งั้นถ้า STM32 เงียบ ค่า C3 จะไม่ขึ้นเลย
    if (c3_new && MQTT_Connected()) {
        c3_new = false;
        MQTT_PubFloat(T_C3_TEMP, current_c3_temp, 1);
    }

    // ── 3. UART RX จาก STM32 ──
    while (Serial2.available()) {
        String line = Serial2.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // DEBUG: บรรทัดไบต์ดิบจากเรดาร์ ส่งต่อออกจอเฉยๆ ไม่ต้อง parse
        // ลบทิ้งได้เมื่อแก้ปัญหา LD2412 จบ
        if (line.startsWith("RAW:")) {
            Serial.println(line);
            continue;
        }

        char cmd[12] = {0};

        if (UART_RX_ParseCmd(line, cmd, sizeof(cmd))) {
            // ปุ่มบน STM32 ถูกกด
            if (strcmp(cmd, "LIGHT_ON") == 0)       applyLight(true,  "button");
            else if (strcmp(cmd, "LIGHT_OFF") == 0) applyLight(false, "button");
        }
        else if (UART_RX_Parse(line, &g_sensor)) {
            publishSensors();
            publishSnapshot();
        }
    }

    // ── 4. ไม่มี state machine แล้ว ──
    // ตรรกะ automation ทั้งหมดย้ายไปอยู่ใน Home Assistant ที่เดียว
    // gateway ตัวนี้ทำหน้าที่แค่ 3 อย่าง: ส่งค่าเซนเซอร์ขึ้นไป,
    // รับคำสั่งจาก HA, และส่งต่อการกดปุ่มที่กล่อง
    //
    // เหตุผล: ถ้าปล่อยให้ firmware ตัดสินใจเองด้วย มันจะสั่งสวนกับ HA
    // เช่น HA สั่งเปิด แล้วอีก 5 วินาที firmware สั่งปิดเพราะไม่เห็นคน
    //
    // ไฟล์ automation.cpp / automation.h ยังเก็บไว้เผื่ออนาคต
    // ถ้าวันหน้าอยากได้ fallback ตอน MQTT หลุด ค่อยกลับมาเปิดใช้

    // ── 5. ไฟเปลี่ยนสถานะ → บอกทั้ง STM32 และ MQTT ──
    if (light_on != prev_light_on) {
        prev_light_on = light_on;
        Serial2.print(light_on ? "STATE:ON\n" : "STATE:OFF\n");
        publishLightState();
        publishSnapshot();
    }

    delay(10);
}
