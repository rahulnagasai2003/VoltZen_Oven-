# VoltZen Oven Unit — ESP32 Firmware

## Overview

This is the **Oven Unit ESP32** firmware for the VoltZen industrial oven monitoring system.
It reads temperature from a J/K thermocouple via **MAX31856** (SPI) and fan currents via two **PZEM-004T** modules (UART),
then publishes all telemetry data to a cloud **MQTT broker** over WiFi.

The device is provisioned via a **SoftAP + HTTP** workflow — a companion Flutter app connects to the oven's AP, sends WiFi + MQTT credentials, and the device reboots into STA mode.

---

## Key Features

| Feature | Details |
|---------|---------|
| **Temperature Monitoring** | Reads J or K type thermocouple via MAX31856 (SPI) every **1 second**. |
| **Fan Current Monitoring** | Reads current from 2× PZEM-004T modules (Fan 1 + Fan 2) every **5 seconds**. |
| **MQTT Telemetry** | Publishes sensor data to a configurable MQTT broker every **5 seconds**. |
| **WiFi Provisioning** | SoftAP mode with HTTP REST endpoint — the companion app sends WiFi + MQTT credentials as JSON. |
| **Runtime TC Type Config** | Thermocouple type (J/K) is configurable at runtime via NVS — no recompilation needed. |
| **NVS Persistence** | WiFi credentials, MQTT details, device ID, TC type, temperature thresholds, and last sensor readings all survive reboots. |
| **Fan State Detection** | Current readings are converted to boolean fan states (ON if current > 0.05A). |
| **Auto-reconnect** | WiFi STA auto-reconnects on disconnection. |

---

## Architecture

```
┌──────────────────┐        ┌──────────────┐        ┌──────────────────┐
│   Flutter App    │──WiFi──│  Oven ESP32   │──MQTT──│  Cloud Broker    │
│  (Provisioning)  │  HTTP  │  (This Repo)  │        │  (HiveMQ/etc.)   │
└──────────────────┘        └──────┬───┬────┘        └──────────────────┘
                              SPI  │   │ UART
                           ┌───────┘   └────────┐
                           │                    │
                      ┌────┴─────┐      ┌───────┴──────┐
                      │ MAX31856 │      │ PZEM-004T ×2 │
                      │(TC Temp) │      │ (Fan Current) │
                      └──────────┘      └──────────────┘
```

---

## Hardware Wiring

### MAX31856 (SPI — Thermocouple)

| MAX31856 Pin | ESP32 GPIO |
|-------------|-----------|
| SDI (MOSI) | **GPIO 23** |
| SDO (MISO) | **GPIO 19** |
| SCLK (SCK) | **GPIO 18** |
| CS | **GPIO 5** |
| VCC | 3.3V |
| GND | GND |

> ⚠️ Connect your J or K thermocouple to the T+ and T- terminals of the MAX31856.
> The thermocouple type is configured from NVS at boot (defaults to K if never set).

### PZEM-004T × 2 (Fan Currents)

| Signal | ESP32 GPIO |
|--------|-----------|
| Fan-1 TX (ESP → PZEM RX) | **GPIO 17** |
| Fan-1 RX (PZEM TX → ESP) | **GPIO 16** |
| Fan-2 TX (ESP → PZEM RX) | **GPIO 22** |
| Fan-2 RX (PZEM TX → ESP) | **GPIO 21** |

### PZEM Modbus Address Programming (one-time)

- Fan-1 → `0x01`
- Fan-2 → `0x02`

---

## Provisioning Flow

The device uses a **SoftAP + HTTP** provisioning model:

```
First Boot (no credentials in NVS)
   │
   ▼
Start SoftAP: "NLX-APTEMP-XXXX" (open, no password)
   │
   ▼
Start HTTP server on port 80
   │
   ▼
Flutter App connects to AP
   │
   ▼
App sends POST to /api/wifi/configure with JSON body
   │
   ▼
ESP32 saves credentials to NVS → sends HTTP 200 → reboots
   │
   ▼
On reboot: loads credentials from NVS → connects as STA
   │
   ▼
WiFi connected → MQTT init → Sensor task starts
```

### Provisioning API

**Endpoint:** `POST /api/wifi/configure`

**Request Body (JSON):**

```json
{
  "ssid": "YourWiFiNetwork",
  "password": "YourWiFiPassword",
  "deviceId": "OvenUnit-001",
  "mqttHost": "broker.hivemq.com",
  "mqttPort": 8883,
  "mqttUsername": "your_mqtt_user",
  "mqttPassword": "your_mqtt_pass"
}
```

**Response:**

```json
{
  "status": "received"
}
```

> The device reboots automatically 2 seconds after sending the response.

---

## Startup Sequence

```
app_main()
   │
   ├── 1. NVS Flash Init
   │       └── Load TC type from NVS (default: "K")
   │       └── Load last known sensor readings
   │
   ├── 2. MAX31856 SPI Init (with loaded TC type)
   │
   ├── 3. PZEM Fan UART Init
   │
   ├── 4. Network Stack Init (esp_netif + event loop + WiFi driver)
   │
   ├── 5. WiFi Provisioning Manager
   │       ├── Credentials found in NVS? → Connect STA mode
   │       └── No credentials? → Start SoftAP + HTTP server
   │
   └── 6. Startup Task (waits for WiFi connection)
           ├── MQTT Client Init (using provisioned broker details)
           └── Sensor Task Start
```

---

## Sensor Task Timing

| Action | Interval |
|--------|----------|
| Read thermocouple temperature | Every **1 second** |
| Read fan 1 + fan 2 current | Every **5 seconds** |
| Publish MQTT telemetry | Every **5 seconds** |

---

## MQTT Telemetry

### Topic Format

```
nt/v1/{deviceId}/stat/telemetry
```

### Payload Format

```json
{
  "temperature": 245.5,
  "fans": [
    { "index": 1, "state": true },
    { "index": 2, "state": false }
  ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `temperature` | float | Thermocouple temperature in °C |
| `fans[].index` | int | Fan number (1 or 2) |
| `fans[].state` | bool | `true` if current > 0.05A (fan running), `false` otherwise |

### MQTT Connection

- Protocol auto-selected: `mqtt://` for port 1883, `mqtts://` for port 8883
- TLS: Insecure mode enabled (skips certificate CN check) — suitable for development
- Client ID: set to the provisioned `deviceId`

---

## File Structure

```
VoltZen_Oven--main/
├── CMakeLists.txt              ← Root project CMake (target: oven_unit)
├── README.md                   ← This file
├── sdkconfig                   ← ESP-IDF build configuration
├── .gitignore
├── main/
│   ├── CMakeLists.txt          ← Component registration + dependencies
│   ├── idf_component.yml      ← IDF Component Manager deps (MQTT, cJSON)
│   ├── main.c                  ← Entry point: NVS → SPI → UART → WiFi → MQTT → Sensors
│   ├── max31856.c / .h         ← Custom SPI driver for MAX31856 (J/K runtime configurable)
│   ├── pzem_fan.c / .h         ← 2-channel PZEM-004T current reader (UART, Modbus)
│   ├── nvs_manager.c / .h      ← NVS read/write (WiFi, TC type, thresholds, last readings)
│   ├── wifi_prov_mgr.c / .h    ← WiFi provisioning: SoftAP + HTTP or STA auto-connect
│   ├── mqtt_mgr.c / .h         ← MQTT client init, event handling, telemetry publish
│   └── sensor_task.c / .h      ← FreeRTOS task: read sensors + publish MQTT telemetry
```

---

## Thermocouple Type Configuration

The oven firmware does **not** need to be recompiled to switch thermocouple types.

1. On first boot, TC type defaults to **K** if no value is found in NVS.
2. The TC type can be saved to NVS via `nvs_save_tc_type("J")` or `nvs_save_tc_type("K")`.
3. On every boot, the saved TC type is loaded from NVS and written to the MAX31856 CR1 register at **runtime**.
4. Supported types: B, E, **J**, **K**, N, R, S, T (all defined in the driver, J and K are primary).

---

## NVS Storage Map

All persistent data is stored in the NVS namespace `oven_cfg`:

| Key | Type | Description |
|-----|------|-------------|
| `ssid` | string | WiFi SSID (emergency WiFi) |
| `password` | string | WiFi password (emergency WiFi) |
| `tc_type` | string | Thermocouple type ("J" or "K") |
| `temp_thresh` | blob | Temperature thresholds (min + max, as floats) |
| `last_data` | blob | Last sensor readings (temp, fan1, fan2) |

WiFi + MQTT provisioning credentials are stored in a separate NVS namespace `storage`:

| Key | Type | Description |
|-----|------|-------------|
| `ssid` | string | WiFi SSID |
| `password` | string | WiFi password |
| `device_id` | string | Device ID for MQTT client |
| `mqttHost` | string | MQTT broker hostname |
| `mqttPort` | i32 | MQTT broker port |
| `mqttUsername` | string | MQTT username |
| `mqttPassword` | string | MQTT password |
| `valid` | u8 | Credential validity marker (0xAB = valid) |

---

## Dependencies

| Component | Source | Purpose |
|-----------|--------|---------|
| `espressif/mqtt` | IDF Component Registry | MQTT client library |
| `espressif/cjson` | IDF Component Registry | JSON serialization for MQTT payloads |
| `esp_wifi` | ESP-IDF | WiFi STA + SoftAP |
| `esp_http_server` | ESP-IDF | HTTP server for provisioning endpoint |
| `nvs_flash` | ESP-IDF | Non-volatile storage |
| `esp_driver_spi` | ESP-IDF | SPI bus for MAX31856 |
| `esp_driver_uart` | ESP-IDF | UART for PZEM-004T Modbus |

---

## Build and Flash

```bash
# Set IDF target
idf.py set-target esp32

# Build
idf.py build

# Flash and monitor
idf.py -p COM_PORT flash monitor
```

> Replace `COM_PORT` with your actual serial port (e.g., `COM3` on Windows, `/dev/ttyUSB0` on Linux).

---

## Important Notes

- **No ESP-NOW**: This firmware communicates directly over WiFi + MQTT. There is no supply unit or ESP-NOW in this architecture.
- **SoftAP is open**: The provisioning AP (`NLX-APTEMP-XXXX`) has no password — intended for initial setup only.
- **Credential persistence**: Once provisioned, the device connects automatically on every boot. To re-provision, erase NVS flash (`idf.py erase-flash`).
- **TLS insecure mode**: MQTT over port 8883 uses TLS but skips certificate validation. For production, configure proper CA certificates.
- **Fan state is derived**: The firmware doesn't read a digital on/off signal — it infers fan state from current draw (>0.05A = running).
- **Data persistence**: Last sensor readings are saved to NVS before each MQTT publish, ensuring no data loss across reboots.
- **Auto-reconnect**: WiFi STA mode automatically retries connection on disconnection events.

---

## License

Proprietary — VoltZen / NLX Technologies.
