# Smart Home IoT System (STM32 + ESP32)

An end-to-end home automation system I run in my own bedroom: an STM32
reads every sensor, hands the readings to an ESP32 that speaks nothing but
WiFi and MQTT, and a separate ESP32-C3 drives the room's relay. Home
Assistant does the automation and the dashboard; a Docker stack on a server
I administer runs the broker and history.

## Why split it across three MCUs

- **STM32F103C8T6** owns every sensor and the on-box display. It is the only
  thing on the I2C bus and the only thing that has to stay real-time, so
  network trouble can never disturb a reading.
- **ESP32 (gateway)** does exactly one job: WiFi and MQTT. It never touches
  a sensor directly — it only relays JSON it gets from the STM32 over UART.
- **ESP32-C3 SuperMini (lamp box)** drives the relay for the room light. It
  takes commands over **ESP-NOW**, not MQTT, because a light switch has to
  answer in well under a second, and ESP-NOW does not wait on the WiFi
  association or the broker.

```
sensors (I2C/UART) → STM32 (packs JSON) → UART → ESP32 gateway → MQTT → Home Assistant
                                                        │
                                                     ESP-NOW
                                                        ▼
                                              ESP32-C3 → relay → lamp
```

## Hardware

| Part | Job | Bus |
|---|---|---|
| STM32F103C8T6 | reads every sensor, drives the display, packs JSON | UART1 to the ESP32 |
| ESP32 | pure WiFi/MQTT gateway | WiFi, UART2, ESP-NOW |
| ESP32-C3 SuperMini | lamp relay, commanded over ESP-NOW | ESP-NOW |
| SHT30 | temperature and humidity | I2C `0x44` |
| BH1750 | ambient light (lux) | I2C `0x23` |
| DS3231 | real-time clock | I2C `0x68` |
| LD2412 | mmWave presence + distance radar | UART2, DMA |
| ST7735 | on-box status display | SPI1 |
| 2 push buttons | display on/off, light on/off | GPIO, debounced in firmware |

## What's in this repo

```
firmware/
  stm32_sensor_node/   STM32CubeIDE project (STM32F103, HAL)
  esp32_gateway/        Arduino sketch — MQTT + Home Assistant discovery
  esp32c3_lamp_node/    Arduino sketch — ESP-NOW relay control
home-assistant/
  automations.yaml      Bedroom lighting automation
  helpers.yaml           input_boolean / input_number / template helpers it needs
```

### STM32 sensor node

Runs a fixed read schedule out of the main loop — no RTOS, just staggered
timers so the sensors don't fight over the I2C bus in the same millisecond:
BH1750 every 500 ms, SHT30 every 2 s, DS3231 every 1 s. LD2412 is handled
separately: it streams frames continuously over UART2 into a DMA buffer, and
`HAL_UARTEx_RxEventCallback` parses them as they arrive.

It only sends a JSON packet over UART1 when something worth reporting
happens — a presence-state change, a lux reading crossing the dark/light
threshold, or a 30-second heartbeat — rather than flooding the gateway on
every loop.

Two things it does on its own that were worth writing up:

- **I2C bus recovery.** If a sensor read fails three times in a row, it
  assumes the bus is wedged, bit-bangs 9 clock pulses on SCL to force a
  stuck slave to let go of SDA (the standard I2C unstick sequence), issues a
  manual STOP condition, and re-initialises the peripheral and every sensor.
- **Two different switches, on purpose.** The display button and the light
  button read as very different things on the pin — one is a maintained
  slide switch, the other a momentary push-button — because that's what
  came off the shelf; the firmware debounces and edge-detects them
  differently rather than pretending they're the same part.

### ESP32 gateway

Publishes Home Assistant MQTT discovery payloads on every reconnect, so
entities show up automatically with no manual YAML per sensor. The device
block and unique IDs are held constant on purpose — get them wrong and Home
Assistant creates duplicate entities instead of reusing the old ones.

A few things that came from running this for real, not from a tutorial:

- **Offline buffering.** If the MQTT publish fails, the snapshot is appended
  to a file in LittleFS instead of being dropped. The next successful
  connection replays the whole backlog before resuming live updates.
- **Discovery also deletes.** A power-metering module (PZEM) was dropped
  from the design after the fact; `publishDiscovery()` still publishes
  empty payloads to its old discovery topics on every connect, which is
  the correct way to tell Home Assistant to remove an entity that no
  longer exists rather than leaving it stuck at its last value forever.
- **No automation logic lives here.** Early versions had the ESP32 make
  its own decisions about the light. That was dropped — with the gateway
  and Home Assistant both deciding, they occasionally fought each other
  (HA turns the light on, five seconds later the firmware turns it back
  off because it doesn't see anyone). The gateway now only relays: sensor
  data up, commands down, button presses across.

### ESP32-C3 lamp node

Talks to the gateway over ESP-NOW using its MAC address rather than
broadcast, and tracks delivery for real: `esp_now_send()` returning `ESP_OK`
only means the packet was queued, not that it arrived, so the node waits for
the actual `OnDataSent` callback before deciding a message succeeded. After
three unacknowledged sends in a row it re-scans for the access point's
current WiFi channel and re-pairs — because ESP-NOW peers are pinned to a
channel, and if the router changes channel (e.g. after a power cut) the
gateway and the lamp node silently stop hearing each other until something
re-syncs.

### Home Assistant

`automations.yaml` is the actual bedroom lighting logic: lights on when
someone walks in and it's dark, lights off after nobody's been seen for a
configurable number of minutes, a separate "asleep" rule that watches for
the radar reporting *static presence* (someone lying still) for a while
before cutting the lights — something a plain PIR sensor cannot do, since a
PIR sees "not moving" as "not there" and cuts the lights on someone who
hasn't fallen asleep yet. A manual-override timer stops the automation from
fighting anyone who turned the light on by hand.

`helpers.yaml` defines every number the automation reads (lux threshold,
empty-room timeout, sleep timeout, day/night brightness, manual-hold
duration) as `input_number` entities, and the night-mode window as a
template `binary_sensor`, so all of that is tunable from the Home Assistant
UI without touching YAML again.

## Building

**STM32:** open `firmware/stm32_sensor_node/stm32_Smarthome.ioc` in
STM32CubeIDE — it will regenerate the HAL drivers and Makefile from the
`.ioc` (they aren't committed here). Build and flash as a normal
STM32F103C8T6 project.

**ESP32 gateway:** Arduino IDE with the ESP32 board package.
Libraries: `PubSubClient`, `ArduinoJson`, `LittleFS` (bundled with the ESP32
core). Copy `secrets.h.example` to `secrets.h` and fill in your WiFi and
MQTT details — `secrets.h` is gitignored and never committed.

**ESP32-C3 lamp node:** Arduino IDE, ESP32 board package (select an
ESP32-C3 board). Update `gateway_mac[]` in the sketch to the gateway's
actual WiFi station MAC (printed on the gateway's serial console at boot).

**Home Assistant:** append `helpers.yaml` to `configuration.yaml` and
restart once; drop `automations.yaml` in as-is or merge it into an existing
automations file. Update the entity IDs in it to match your own light and
switch names.
