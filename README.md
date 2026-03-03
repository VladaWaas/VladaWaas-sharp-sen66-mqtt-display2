# Sharp Memory LCD + SEN66 Environmental Monitor

Indoor air quality monitoring station with a **Sharp Memory LCD** display and **Sensirion SEN66** environmental sensor, connected to **Home Assistant** via MQTT.

Built on **ESP32-C3-MINI1** using PlatformIO + Arduino framework.

## 📷 3D Printed Case

A custom 3D-printable enclosure for this project is available on Printables:

**🔗 [Case for SEN-66 with Sharp Memory Display](https://www.printables.com/model/1600651-case-for-sen-66-with-sharp-memory-display)**

## Features

### Sensors (Sensirion SEN66)
- **Particulate Matter**: PM1.0, PM2.5, PM4.0, PM10 (µg/m³)
- **Temperature** (°C) and **Humidity** (%)
- **VOC Index** and **NOx Index**
- **CO2** (ppm)

### Display (Sharp LS027B7DH01, 400×240)
- Real-time sensor dashboard with all measured values
- Air quality rating (Excellent → Hazardous) with visual bar
- Status bar showing WiFi, MQTT and sensor connection state
- Remote text/graphics display via MQTT commands

### Home Assistant Integration
- **MQTT Auto-Discovery** — sensors appear automatically in HA
- Individual topics for each measurement + combined JSON
- Online/offline status with Last Will and Testament
- Bidirectional control — send messages to the display from HA
- Built-in web UI (tabs: **Aktuální data** + **Konfigurace**) on port 80
<<<<<<< ours
- Optional TMEP.cz HTTP upload with tokenized query parameters
=======
- TMEP.cz integration with real-time request URL preview and manual request button in web UI
>>>>>>> theirs
- MQTT publish protection: invalid startup values are filtered + warmup delay before first publish

## Hardware

| Component | Connection |
|-----------|------------|
| ESP32-C3-MINI1 | USB for power and programming |
| Sharp LS027B7DH01 (2.7" 400×240) | SPI: CLK=GPIO6, MOSI=GPIO7, CS=GPIO3 |
| Sensirion SEN66 | I2C: SDA=GPIO10, SCL=GPIO8 (addr 0x6B) |

### Pin Mapping

```
ESP32-C3 Pin    Function        Peripheral
───────────────────────────────────────────
GPIO6           SPI CLK         Sharp LCD
GPIO7           SPI MOSI        Sharp LCD
GPIO2           SPI MISO        (unused)
GPIO3           SPI CS          Sharp LCD
GPIO10          I2C SDA         SEN66
GPIO8           I2C SCL         SEN66
```

## Installation

### Prerequisites
- [VS Code](https://code.visualstudio.com/) with [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension
- ESP32-C3 board connected via USB

### Steps

1. **Clone the repository**
   ```bash
   git clone https://github.com/VladaWaas/sharp-sen66-mqtt-display.git
   cd sharp-sen66-mqtt-display
   ```

2. **Open in VS Code**
   ```
   File → Open Folder → select sharp-sen66-mqtt-display
   ```

3. **Edit initial configuration** in `src/main.cpp` (WiFi + MQTT defaults).

4. **Build and Upload**
   - Press `Ctrl+Shift+P` → type `PlatformIO: Upload` → Enter
   - Or click the **→** (arrow) icon in the blue bottom bar

5. **Monitor serial output**
   - Press `Ctrl+Shift+P` → type `PlatformIO: Serial Monitor`



## Initial setup / Captive portal

Firmware nově podporuje provisioning přes **AP + captive portal**:

1. Po bootu se načte uložené Wi-Fi z NVS.
2. Pokud SSID chybí, nebo se STA nepřipojí do ~20 s, zařízení přepne do AP režimu.
3. AP má název `SharpDisplay-<MAC>` (poslední 3 byty MAC), výchozí režim je **open AP** (bez hesla).
4. DNS server odpovídá na všechny domény IP adresou AP (`192.168.4.1`) a HTTP požadavky jsou přesměrovány na `/`.
5. V portálu běží stejná web UI stránka jako v normálním režimu, rozšířená o sekci **Wi-Fi setup**:
   - uložit SSID + heslo (`/api/wifi/save`)
   - zapomenout Wi-Fi (`/api/wifi/forget`)
6. Po uložení Wi-Fi se zařízení pokusí o STA připojení a následně se restartuje.

Režimy Wi-Fi v firmware:
- `WIFI_STA_CONNECTING`
- `WIFI_STA_CONNECTED`
- `WIFI_AP_CAPTIVE`

## Web Interface

After connecting the device to WiFi, open: `http://<device-ip>/`

### Tabs

1. **Aktuální data**
   - Live values from SEN66 (temperature, humidity, PM, VOC, NOx, CO2)
   - WiFi/MQTT status and uptime

2. **Konfigurace**
   - WiFi: SSID + password
   - MQTT: server, port, username, password
   - Display: rotation (0-3, výchozí **2**), invert request
   - TMEP.cz: base URL, live preview of real request URL, manual request trigger
   - Intervals: display refresh, MQTT publish, **TMEP request interval**, MQTT warmup delay

> Konfigurace se ukládá perzistentně do NVS (zůstane po restartu). Po uložení z webu se zařízení automaticky restartuje.


## TMEP.cz Upload

The device can optionally send every measured sample to TMEP.cz via HTTP GET.

Web config fields:
- **Doména pro zasílání hodnot**: e.g. `xxk4sk-g6rxfh`
- **Parametry požadavku**: e.g. `tempV=*TEMP*&humV=*HUM*&rssi=*RSSI*`

Supported placeholders:
- Sensor values: `*TEMP*`, `*HUM*`, `*PM1*`, `*PM2*` (PM2.5), `*PM4*`, `*PM10*`, `*VOC*`, `*NOX*`, `*CO2*`
- System values: `*RSSI*`, `*UPTIME*`, `*FREEHEAP*`, `*IP*`

Configuration is persisted in NVS together with the rest of the settings.

## MQTT Startup Data Protection

To avoid sending invalid first values after restart (e.g. CO2 > 65000), firmware now:
- validates sensor ranges before accepting data
- waits default **60s** (`mqttWarmupDelay`) from the first valid sample before MQTT publish

## MQTT Topics

### Subscribe (incoming — display control)

| Topic | Payload | Description |
|-------|---------|-------------|
| `sharp/display/text` | `"Hello!"` | Show text for 30 seconds |
| `sharp/display/clear` | `""` | Clear display, return to dashboard |
| `sharp/display/command` | JSON | Advanced commands (see below) |

### Publish (outgoing — sensor data)

| Topic | Example | Description |
|-------|---------|-------------|
| `sharp/sensor/temperature` | `23.4` | Temperature °C |
| `sharp/sensor/humidity` | `45.2` | Humidity % |
| `sharp/sensor/pm25` | `8.7` | PM2.5 µg/m³ |
| `sharp/sensor/co2` | `832` | CO2 ppm |
| `sharp/sensor/voc` | `125` | VOC Index |
| `sharp/sensor/nox` | `12` | NOx Index |
| `sharp/sensor/pm1` | `5.2` | PM1.0 µg/m³ |
| `sharp/sensor/pm4` | `9.1` | PM4.0 µg/m³ |
| `sharp/sensor/pm10` | `10.3` | PM10 µg/m³ |
| `sharp/sensor` | `{...}` | All values as JSON |
| `sharp/status` | `online` | Online/offline status |

### JSON Commands (`sharp/display/command`)

```json
// Display text with position, size and duration
{"text": "Hello!", "x": 50, "y": 100, "size": 4, "duration": 60}

// Draw a line
{"line": {"x1": 0, "y1": 120, "x2": 399, "y2": 120}}

// Draw a rectangle (filled or outline)
{"rect": {"x": 10, "y": 10, "w": 100, "h": 50, "fill": true}}

// Switch back to sensor dashboard
{"dashboard": true}
```

## Home Assistant Examples

### Send a notification to the display

```yaml
automation:
  - alias: "Display — Window Open Warning"
    trigger:
      - platform: state
        entity_id: binary_sensor.window
        to: "on"
    action:
      - service: mqtt.publish
        data:
          topic: "sharp/display/command"
          payload: >
            {"text":"WINDOW OPEN!","x":30,"y":80,"size":3,"duration":120}
```

### Air quality alert

```yaml
automation:
  - alias: "Alert — Poor Air Quality"
    trigger:
      - platform: numeric_state
        entity_id: sensor.sen66_pm25
        above: 35
        for:
          minutes: 5
    action:
      - service: notify.persistent_notification
        data:
          title: "⚠️ Air Quality Warning"
          message: "PM2.5 is {{ states('sensor.sen66_pm25') }} µg/m³"
```

### CO2 ventilation reminder

```yaml
automation:
  - alias: "Ventilation — High CO2"
    trigger:
      - platform: numeric_state
        entity_id: sensor.sen66_co2
        above: 1000
        for:
          minutes: 10
    action:
      - service: mqtt.publish
        data:
          topic: "sharp/display/command"
          payload: >
            {"text":"VENTILATE!\nCO2: {{ states('sensor.sen66_co2') }} ppm","x":30,"y":70,"size":3,"duration":600}
```

### Lovelace dashboard card

```yaml
type: entities
title: "🌡 Environmental Station"
entities:
  - entity: sensor.sen66_temp
    name: Temperature
  - entity: sensor.sen66_humidity
    name: Humidity
  - type: divider
  - entity: sensor.sen66_pm25
    name: PM2.5
  - entity: sensor.sen66_co2
    name: CO2
  - entity: sensor.sen66_voc
    name: VOC Index
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Display is blank | Check SPI pins (6, 7, 3) and 3.3V power |
| MQTT error rc=5 | Check MQTT username and password |
| SEN66 error | Check I2C pins (10, 8) and sensor cable |
| WiFi FAILED | Check SSID and password |
| Sensors not in HA | Verify MQTT integration in HA, restart |

## Dependencies

Managed automatically by PlatformIO:

- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit SHARP Memory Display](https://github.com/adafruit/Adafruit_SHARP_Memory_Display)
- [Sensirion I2C SEN66](https://github.com/Sensirion/arduino-i2c-sen66)
- [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)

## License

MIT License — see [LICENSE](LICENSE) file.

## Author

**Vladimír Waas** — [@VladaWaas](https://github.com/VladaWaas)
