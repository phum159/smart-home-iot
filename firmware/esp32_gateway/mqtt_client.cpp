#include "mqtt_client.h"
#include "secrets.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient     wifi_client;
static PubSubClient   mqtt(wifi_client);
static MqttCmdHandler cmd_handler = nullptr;
static uint32_t       last_try   = 0;
static const uint32_t RETRY_MS   = 5000;

// device block ที่ต้องตรงกับของเดิมเป๊ะๆ ไม่งั้น HA จะสร้าง device ใหม่ซ้ำ
#define DEV_BOX1  "\"device\":{\"identifiers\":[\"smarthome_box1_gateway\"],\"name\":\"Smart Home Control Box\",\"manufacturer\":\"DIY\"}"
#define DEV_BOX3  "\"device\":{\"identifiers\":[\"smarthome_box3_lamp\"],\"name\":\"Smart Home Lamp Box\",\"manufacturer\":\"DIY\"}"
#define AVTY      "\"availability_topic\":\"" T_STATUS "\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\""

static void onMessage(char *topic, byte *payload, unsigned int len) {
    char buf[32];
    unsigned int n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    if (cmd_handler) cmd_handler(topic, buf);
}

// ── ประกาศ entity ให้ HA รู้จัก (retained บน homeassistant/<comp>/<uid>/config) ──
static void publishDiscovery(void) {
    char t[64];
    char p[512];

    // ---- Box1: SHT30 / BH1750 ----
    snprintf(t, sizeof(t), "homeassistant/sensor/box1_temp/config");
    snprintf(p, sizeof(p), "{\"name\":\"Box1 Temperature\",\"state_topic\":\"" T_TEMP "\",\"unique_id\":\"box1_temp\",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\",\"state_class\":\"measurement\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/sensor/box1_hum/config");
    snprintf(p, sizeof(p), "{\"name\":\"Box1 Humidity\",\"state_topic\":\"" T_HUM "\",\"unique_id\":\"box1_hum\",\"device_class\":\"humidity\",\"unit_of_measurement\":\"%%\",\"state_class\":\"measurement\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/sensor/box1_lux/config");
    snprintf(p, sizeof(p), "{\"name\":\"Box1 Illuminance\",\"state_topic\":\"" T_LUX "\",\"unique_id\":\"box1_lux\",\"device_class\":\"illuminance\",\"unit_of_measurement\":\"lx\",\"state_class\":\"measurement\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    // ---- Box1: อุณหภูมิชิป ----
    snprintf(t, sizeof(t), "homeassistant/sensor/box1_stm_temp/config");
    snprintf(p, sizeof(p), "{\"name\":\"STM32 Temperature\",\"state_topic\":\"" T_STM32_TEMP "\",\"unique_id\":\"box1_stm_temp\",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\",\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/sensor/box1_esp_temp/config");
    snprintf(p, sizeof(p), "{\"name\":\"ESP32 Chip Temperature\",\"state_topic\":\"" T_ESP32_TEMP "\",\"unique_id\":\"box1_esp_temp\",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\",\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    // ---- Box1: LD2412 (ของใหม่) ----
    snprintf(t, sizeof(t), "homeassistant/binary_sensor/box1_presence/config");
    snprintf(p, sizeof(p), "{\"name\":\"Presence\",\"state_topic\":\"" T_PRESENCE "\",\"unique_id\":\"box1_presence\",\"device_class\":\"occupancy\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/sensor/box1_ld_state/config");
    snprintf(p, sizeof(p), "{\"name\":\"Presence State\",\"state_topic\":\"" T_LD_STATE "\",\"unique_id\":\"box1_ld_state\",\"icon\":\"mdi:radar\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/sensor/box1_m_dist/config");
    snprintf(p, sizeof(p), "{\"name\":\"Motion Distance\",\"state_topic\":\"" T_M_DIST "\",\"unique_id\":\"box1_m_dist\",\"device_class\":\"distance\",\"unit_of_measurement\":\"cm\",\"state_class\":\"measurement\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/sensor/box1_s_dist/config");
    snprintf(p, sizeof(p), "{\"name\":\"Static Distance\",\"state_topic\":\"" T_S_DIST "\",\"unique_id\":\"box1_s_dist\",\"device_class\":\"distance\",\"unit_of_measurement\":\"cm\",\"state_class\":\"measurement\"," AVTY "," DEV_BOX1 "}");
    mqtt.publish(t, p, true);

    // ---- Box3 ----
    snprintf(t, sizeof(t), "homeassistant/sensor/box3_c3_temp/config");
    snprintf(p, sizeof(p), "{\"name\":\"ESP32-C3 Chip Temperature\",\"state_topic\":\"" T_C3_TEMP "\",\"unique_id\":\"box3_c3_temp\",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\",\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"," AVTY "," DEV_BOX3 "}");
    mqtt.publish(t, p, true);

    snprintf(t, sizeof(t), "homeassistant/switch/box3_relay/config");
    snprintf(p, sizeof(p), "{\"name\":\"Lamp Box Relay\",\"state_topic\":\"" T_RELAY_STATE "\",\"command_topic\":\"" T_RELAY_SET "\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"unique_id\":\"box3_relay\"," AVTY "," DEV_BOX3 "}");
    mqtt.publish(t, p, true);

    // ---- ล้าง PZEM ที่ถอดออกจากระบบแล้ว (payload ว่าง = ลบ entity ทิ้ง) ----
    mqtt.publish("homeassistant/sensor/box1_volt/config",   "", true);
    mqtt.publish("homeassistant/sensor/box1_curr/config",   "", true);
    mqtt.publish("homeassistant/sensor/box1_power/config",  "", true);
    mqtt.publish("homeassistant/sensor/box1_energy/config", "", true);

    Serial.println("MQTT: discovery published (PZEM cleared)");
}

static bool reconnect(void) {
    if (WiFi.status() != WL_CONNECTED) return false;

    // LWT: ถ้าหลุดกะทันหัน broker จะ publish "offline" ให้เอง
    bool ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                           T_STATUS, 0, true, "offline");
    if (!ok) {
        Serial.printf("MQTT: connect failed rc=%d\n", mqtt.state());
        return false;
    }

    mqtt.publish(T_STATUS, "online", true);
    mqtt.subscribe(T_RELAY_SET);
    mqtt.subscribe(T_LIGHT_SET);
    publishDiscovery();
    Serial.println("MQTT: connected");
    return true;
}

void MQTT_Init(MqttCmdHandler handler) {
    cmd_handler = handler;
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(onMessage);
    mqtt.setBufferSize(1024);   // discovery payload ยาวเกิน default 256 ไบต์
    mqtt.setKeepAlive(30);
}

void MQTT_Loop(void) {
    if (mqtt.connected()) {
        mqtt.loop();
        return;
    }
    uint32_t now = millis();
    if (now - last_try >= RETRY_MS) {
        last_try = now;
        reconnect();
    }
}

bool MQTT_Connected(void) { return mqtt.connected(); }

bool MQTT_Pub(const char *topic, const char *payload, bool retain) {
    if (!mqtt.connected()) return false;
    return mqtt.publish(topic, payload, retain);
}

bool MQTT_PubFloat(const char *topic, float value, uint8_t decimals) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return MQTT_Pub(topic, buf);
}

bool MQTT_PubInt(const char *topic, int value) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    return MQTT_Pub(topic, buf);
}
