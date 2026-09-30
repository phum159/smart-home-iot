#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>

// ══════════════════════════════════════════════════════════
//  MQTT Topic Tree
//  ต้องตรงกับ retained discovery ที่ HA ลงทะเบียนไว้แล้ว
//  ไม่งั้น entity จะซ้ำใน Home Assistant
// ══════════════════════════════════════════════════════════

// ── availability (LWT) ──
#define T_STATUS          "home/box1/status"

// ── Box1 sensors ──
#define T_TEMP            "home/box1/sht30/temperature"
#define T_HUM             "home/box1/sht30/humidity"
#define T_LUX             "home/box1/bh1750/lux"
#define T_STM32_TEMP      "home/box1/stm32/temperature"
#define T_ESP32_TEMP      "home/box1/esp32/temperature"

// ── Box1 LD2412 (ของใหม่) ──
#define T_PRESENCE        "home/box1/ld2412/presence"
#define T_LD_STATE        "home/box1/ld2412/state"
#define T_M_DIST          "home/box1/ld2412/motion_distance"
#define T_S_DIST          "home/box1/ld2412/static_distance"

// ── Box1 light (ของใหม่) ──
#define T_LIGHT_STATE     "home/box1/light/state"
#define T_LIGHT_SET       "home/box1/light/set"

// ── snapshot รวมทุกค่า เอาไว้ให้ FastAPI ดูดทีหลัง + ใช้ replay ตอน offline ──
#define T_SNAPSHOT        "home/box1/state"

// ── Box3 (ESP32-C3 + relay) ──
#define T_C3_TEMP         "home/box3/esp32c3/temperature"
#define T_RELAY_STATE     "home/box3/relay/state"
#define T_RELAY_SET       "home/box3/relay/set"

typedef void (*MqttCmdHandler)(const char *topic, const char *payload);

void MQTT_Init(MqttCmdHandler handler);
void MQTT_Loop(void);              // เรียกถี่ๆ ใน loop() — จัดการ reconnect เองในตัว
bool MQTT_Connected(void);
bool MQTT_Pub(const char *topic, const char *payload, bool retain = true);
bool MQTT_PubFloat(const char *topic, float value, uint8_t decimals = 1);
bool MQTT_PubInt(const char *topic, int value);

#endif
