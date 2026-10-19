# Sharp Memory LCD + SEN66 - Air Quality Monitor

**English** | [Česky](README.cs.md)

Indoor air quality station with a **Sharp Memory LCD** display and a **Sensirion SEN66** environmental sensor, connected to **Home Assistant** via MQTT.

Built on **ESP32-C3-MINI1** with PlatformIO and the Arduino framework.

Current firmware: **2.2.1** - see [CHANGELOG.md](CHANGELOG.md) (in Czech).

![SEN66 assembly](SEN66-asm2%20v11.png)

## 3D-printed case

A printable enclosure is available on Printables:

**[Case for SEN-66 with Sharp Memory Display](https://www.printables.com/model/1600651-case-for-sen-66-with-sharp-memory-display)**

## Features

### Sensirion SEN66 sensor

- **Particulate matter**: PM1.0, PM2.5, PM4.0, PM10 (µg/m³)
- **Temperature** (°C) and **humidity** (%) with a configurable calibration offset
- **VOC Index** and **NOx Index**
- **CO2** (ppm) - validated separately, as it takes about a minute to warm up

### Sharp LS027B7DH01 display (400×240)

- Real-time dashboard with all measured values
- Air quality rating with a visual bar based on PM2.5
- Status bar showing Wi-Fi, TMEP, MQTT and sensor state, plus uptime
- U8g2 sans-serif fonts rendered at native sizes, with Czech accents and proper unit symbols
- Temperature, humidity and CO₂ use large 34-pixel digits: three columns in landscape, with CO₂ below temperature/humidity in portrait. PM, VOC and NOx remain visible in the compact lower section.
- Inverted CO₂ ventilation alert at 2,000 ppm; dashes for gas readings that are not ready
- Landscape and portrait layouts, with matching typography on startup and OTA screens
- Remote text and graphics rendering via MQTT
- Software color inversion (the panel has no hardware inversion)

Fonts are compiled into the firmware; no extra files are needed on the device.
See [src/fonts/README.md](src/fonts/README.md) for sources, licensing and regeneration.
After `pio pkg install`, run `python scripts/test_display.py` to render and test
the dashboard on the host using GCC, Clang or MSVC. Previews are written to
`.pio/display-tests/`; installing Pillow adds PNG output alongside PBM.

### Home Assistant integration

- **MQTT auto-discovery** - sensors appear automatically as a single device
- A separate topic per measurement plus a combined JSON topic
- Online/offline availability via Last Will and Testament
- `state_class: measurement`, so Home Assistant keeps long-term statistics
- Discovery is re-sent when Home Assistant restarts (the device listens for the birth message)
- `unique_id` and the MQTT client ID include the MAC address, so multiple units can coexist
- Two-way control - send messages to the display from Home Assistant

### Configuration

- Wi-Fi onboarding via an **access point and captive portal** (no credentials in the source code)
- Built-in web UI on port 80, optionally password-protected
- **Over-the-air firmware updates (OTA)** from the web UI - just upload `firmware.bin` from a GitHub release
- All settings are stored in NVS
- Optional upload to **TMEP.cz** via HTTP GET with templated parameters

### Live web dashboard

- Responsive cards with Czech labels, localized numbers and units (°C, %, µg/m³, ppm). VOC and NOₓ are dimensionless indices.
- Color and text indicators for CO₂, PM2.5, PM10, VOC and NOₓ, with thresholds explained in an expandable legend. These are indicative bands for instantaneous readings, not health limits or an AQI calculation. PM1 and PM4 are shown without a rating.
- Refreshes every two seconds with connection states and readable uptime. Invalid readings, gas sensor warmup and connection failures show a dash with an explanation.

> The on-device texts (display, web UI, Home Assistant entity names) are in Czech.

## Hardware

| Component | Wiring |
|-----------|--------|
| ESP32-C3-MINI1 | USB for power and programming |
| Sharp LS027B7DH01 (2.7" 400×240) | SPI: CLK=GPIO6, MOSI=GPIO7, CS=GPIO3 |
| Sensirion SEN66 | I2C: SDA=GPIO10, SCL=GPIO8 (address 0x6B) |

### Pin overview

```
ESP32-C3 pin     Function        Peripheral
───────────────────────────────────────────
GPIO6            SPI CLK         Sharp LCD
GPIO7            SPI MOSI        Sharp LCD
GPIO2            SPI MISO        (unused, the panel is write-only)
GPIO3            SPI CS          Sharp LCD
GPIO10           I2C SDA         SEN66
GPIO8            I2C SCL         SEN66
```

## Installation

### Requirements

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension
- An ESP32-C3 board connected via USB

### Steps

1. **Clone the repository**

   ```bash
   git clone https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2.git
   cd VladaWaas-sharp-sen66-mqtt-display2
   ```

2. **Open the project in VS Code** - `File → Open Folder` and select the cloned folder.

3. **Build and upload** - `Ctrl+Shift+P` → `PlatformIO: Upload`, or the **→** arrow in the blue status bar.

4. **Watch the serial output** - `Ctrl+Shift+P` → `PlatformIO: Serial Monitor` (115200 baud).

To skip building, use the ready-made files from [GitHub Releases](https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2/releases) - see [Firmware updates](#firmware-updates).

Nothing needs to be edited in the source files. Defaults live in `src/config.h` and everything is configured at runtime through the captive portal and the web UI.

## First boot and captive portal

1. On startup, the firmware loads the stored Wi-Fi credentials from NVS.
2. If no SSID is stored, or the connection does not succeed within about 20 seconds, the device switches to access point mode.
3. The access point is named `SharpDisplay-<MAC>` (last three bytes of the MAC address) and is **open by default**.
4. A DNS server answers every domain with the access point address (`192.168.4.1`) and HTTP requests are redirected to `/`.
5. The portal offers the same web UI as normal operation, including the **Wi-Fi** section:
   - save SSID and password (`/api/wifi/save`)
   - forget Wi-Fi (`/api/wifi/forget`)
6. After saving, the device restarts and connects as a client.

Wi-Fi states: `WIFI_STA_CONNECTING` → `WIFI_STA_CONNECTED`, with `WIFI_AP_CAPTIVE` as the fallback.

### Wi-Fi outage

If a device with a stored SSID cannot connect (e.g. while the router restarts), it switches to the fallback access point, but **retries the original network every 2 minutes** and reconnects on its own once it is back. The access point keeps running in the meantime. If someone is connected to it, the retry is postponed so their configuration session is not interrupted.

In fallback mode the web UI is **password-protected** just like in normal operation. Only the initial setup, i.e. while no SSID is stored, is open without a password.

## Web UI

Once the device is on your network, open `http://<device-ip>/`.

### Current data tab (Aktuální data)

Live SEN66 values, Wi-Fi and MQTT state, TMEP state and uptime.

### Configuration tab (Konfigurace)

| Group | Settings |
|-------|----------|
| Wi-Fi | SSID, password (can be cleared for an open network), save Wi-Fi only, forget Wi-Fi |
| MQTT | server, port, user, password, client ID, **base topic**, **Home Assistant discovery prefix** |
| Web UI security | user and password for HTTP Basic Auth (cleared password = no login) |
| TMEP.cz | domain, request parameters, preview of the actual URL, manual send |
| Display | rotation (0-3, default **2**), inversion (0/1) |
| Intervals | display refresh, MQTT publish, TMEP request, MQTT warmup delay |
| Calibration | temperature offset in °C (default **−2.0**), added to the measured value |

Configuration is written to NVS and survives a restart. Saving restarts the device.

> **The API never returns passwords.** `GET /api/config` only returns the flags
> `wifiPasswordSet` / `mqttPasswordSet` / `webPasswordSet` (the form uses them to show
> "set" / "not set"). Leaving a password field empty keeps the stored value. To remove a
> password, tick **Smazat heslo** (clear password) next to the field (in the API, send an
> empty string).

> The dashboard layout assumes landscape rotations (0 and 2). Rotations 1 and 3 do rotate
> the panel, but there is no layout for 240×400.

### Update tab (Aktualizace)

Uploading new firmware over the network - see [Firmware updates](#firmware-updates).

## Firmware updates

Every release on [GitHub Releases](https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2/releases) contains two ready-made files built by GitHub Actions:

| File | Purpose | Configuration |
|------|---------|---------------|
| `firmware.bin` | Update via the web UI (OTA) | kept |
| `factory.bin` | Clean install over USB at address `0x0` | **erased** (Wi-Fi, MQTT, passwords) |

### Via the web UI (OTA) - from version 2.2.1

1. Download `firmware.bin` from the desired release.
2. In the web UI, open the **Aktualizace** tab, select the file and click **Nahrát a restartovat** (upload and restart).
3. The device verifies the firmware, restarts and comes back with the new version in about 20 seconds.

OTA only works while the device is connected to Wi-Fi (it is disabled in the fallback access point) and is protected by the same password as the web UI. A corrupted or wrong file is rejected and the device keeps running the previous version.

### Over USB

Versions older than 2.2.1 cannot do OTA, so you need to move to it once over USB:

- **keeping the configuration**: `PlatformIO: Upload` from the cloned repository (see [Installation](#installation)),
- **clean install**: `factory.bin` at address `0x0`, e.g. with the web-based [ESP Tool](https://espressif.github.io/esptool-js/) in Chrome/Edge. This also helps if you forgot the web UI password.

## Home Assistant setup

The firmware needs an MQTT broker. Home Assistant **does not include one** - it is installed separately. The steps below apply to a standard Home Assistant OS or Supervised installation.

### 1. Install the Mosquitto broker

1. Open **Settings → Add-ons → Add-on store** (in newer versions **Settings → Apps → Install app**).
2. Find **Mosquitto broker** - the official one, maintained by the Home Assistant team - and click **Install**.
3. Enable **Start on boot** and **Watchdog**, then **start** the add-on.
4. Check the add-on log; a clean start means the broker is listening.

The default configuration needs no changes. Ports: **1883** plain MQTT, 1884 websockets. If you fill in `certfile` and `keyfile` in the add-on settings, ports 8883 and 8884 for TLS are added - the plain ports keep working until you remove them manually in the add-on's network section.

### 2. Create a dedicated MQTT user

The add-on authenticates against Home Assistant's user system, so create a regular user for the device:

1. **Settings → People → Users → Add user.**
2. Pick a name such as `mqtt-sensors` and a strong password.

Two important restrictions:

- The user **must not be named `homeassistant` or `addons`** - both names are reserved for the internal ecosystem.
- If you do not see the *Users* tab, enable **Advanced mode** in your profile.

The add-on **does not support anonymous connections** - `allow_anonymous true` does not work, so a username and password are required.

### 3. Add the MQTT integration

1. Go to **Settings → Devices & services**.
2. MQTT usually appears at the top among discovered integrations. If not, use **Add integration → MQTT**.
3. Confirm that Home Assistant should use the broker from the Mosquitto add-on, and check that **enable discovery** is ticked.

Home Assistant generates and manages its own broker credentials - the user from step 2 is for the ESP32, not for Home Assistant.

### 4. Point the device at the broker

In the device web UI, **Konfigurace → MQTT**:

| Field | Value |
|-------|-------|
| Server | **LAN IP address of the machine running Home Assistant**, e.g. `192.168.1.10` |
| Port | `1883` |
| User | the user from step 2, e.g. `mqtt-sensors` |
| Password | that user's password |
| Base topic | `sharp` (change it if you run more than one unit) |
| Discovery prefix | `homeassistant` (change only if you changed it in the MQTT integration) |

Enter the IP address, not `core-mosquitto` - that hostname only resolves inside Home Assistant's internal Docker network and the ESP32 cannot reach it.

Save. The device restarts, connects and sends retained discovery messages. Within a few seconds a new device **Sharp SEN66 Displej** with nine entities appears under the MQTT integration.

### 5. Verification

If the entities do not appear:

1. **Settings → Devices & services → MQTT → Configure.**
2. In **Listen to a topic** enter `sharp/#` (or `#` for everything) and click **Start listening**.
3. Sensor values should arrive, and `sharp/status` should be `online`.
4. Also listen to `homeassistant/sensor/#` to confirm the discovery messages arrived.

The serial monitor tells the other half of the story - `MQTT: CHYBA rc=5` means wrong credentials, `rc=-2` an unreachable broker.

Note that after the first valid sample the device waits **60 seconds** (`mqttWarmupDelay`) before publishing anything (to MQTT or TMEP). Nothing shows up right after a restart. The delay keeps bogus warm-up values out of your history.

### Optional: restrict topics with ACLs

To limit the device account to its own topics, enable the `customize` option in the add-on and add an ACL file. Home Assistant expects the `homeassistant` and `addons` users to keep unrestricted read and write access to all topics - leaving them out breaks your installation. The add-on documentation describes the exact file format.

## Sending data to TMEP.cz

The device can optionally send each sample to TMEP.cz via HTTP GET.

Web UI fields:

- **Domain** - e.g. `xxk4sk-g6rxfh` (the request goes to `http://<domain>.tmep.cz/`)
- **Request parameters** - e.g. `tempV=*TEMP*&humV=*HUM*&rssi=*RSSI*`

Supported variables (both `*NAME*` and `{NAME}` forms work):

| From the sensor | System |
|-----------------|--------|
| `*TEMP*`, `*HUM*` | `*RSSI*` |
| `*PM1*`, `*PM2*` (PM2.5), `*PM4*`, `*PM10*` | `*UPTIME*` (seconds) |
| `*VOC*`, `*NOX*` | `*FREEHEAP*` (bytes) |
| `*CO2*` | `*IP*` |

`*CO2*` is replaced with an empty string until the CO2 channel has warmed up.

## MQTT topics

All topics below derive from the configurable base topic, `sharp` by default.

### Subscribed (incoming, display control)

| Topic | Payload | Description |
|-------|---------|-------------|
| `sharp/display/text` | `"Hello!"` | Shows text for 30 seconds |
| `sharp/display/clear` | `""` | Clears the display |
| `sharp/display/command` | JSON | Advanced commands (see below) |
| `homeassistant/status` | `online` | Home Assistant birth message, triggers re-sending discovery |

### Published (outgoing, sensor data)

| Topic | Example | Description |
|-------|---------|-------------|
| `sharp/sensor/temperature` | `23.4` | Temperature °C |
| `sharp/sensor/humidity` | `45.2` | Humidity % |
| `sharp/sensor/pm1` | `5.2` | PM1.0 µg/m³ |
| `sharp/sensor/pm25` | `8.7` | PM2.5 µg/m³ |
| `sharp/sensor/pm4` | `9.1` | PM4.0 µg/m³ |
| `sharp/sensor/pm10` | `10.3` | PM10 µg/m³ |
| `sharp/sensor/voc` | `125` | VOC Index |
| `sharp/sensor/nox` | `12` | NOx Index |
| `sharp/sensor/co2` | `832` | CO2 ppm (not sent until valid) |
| `sharp/sensor` | `{...}` | All values as JSON |
| `sharp/status` | `online` | Availability (retained, LWT). Also `offline` when no valid sensor data arrived for 30 s |

All sensor data messages are published as retained.

### JSON commands (`sharp/display/command`)

```jsonc
// Text with position, size and display duration
{"text": "Hello!", "x": 50, "y": 100, "size": 4, "duration": 60}

// Line
{"line": {"x1": 0, "y1": 120, "x2": 399, "y2": 120}}

// Rectangle (filled or outline)
{"rect": {"x": 10, "y": 10, "w": 100, "h": 50, "fill": true}}

// Color inversion (software, not stored in NVS)
{"invert": true}

// Back to the sensor dashboard
{"dashboard": true}
```

## Home Assistant examples

The entity IDs below are what Home Assistant derives from the discovery messages (the entity names are Czech, e.g. *teplota* = temperature, *vlhkost* = humidity). Check yours in **Settings → Devices & services → MQTT**.

### Send an alert to the display

```yaml
automation:
  - alias: "Display - open window alert"
    triggers:
      - trigger: state
        entity_id: binary_sensor.window
        to: "on"
    actions:
      - action: mqtt.publish
        data:
          topic: "sharp/display/command"
          payload: >
            {"text":"WINDOW OPEN!","x":30,"y":80,"size":3,"duration":120}
```

### Alert on poor air quality

```yaml
automation:
  - alias: "Warning - poor air quality"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.sharp_sen66_displej_pm2_5
        above: 35
        for:
          minutes: 5
    actions:
      - action: notify.persistent_notification
        data:
          title: "Air quality warning"
          message: "PM2.5 is {{ states('sensor.sharp_sen66_displej_pm2_5') }} µg/m³"
```

### Ventilation reminder on high CO2

```yaml
automation:
  - alias: "Ventilation - high CO2"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.sharp_sen66_displej_co2
        above: 1000
        for:
          minutes: 10
    actions:
      - action: mqtt.publish
        data:
          topic: "sharp/display/command"
          payload: >
            {"text":"VENTILATE!\nCO2: {{ states('sensor.sharp_sen66_displej_co2') }} ppm","x":30,"y":70,"size":3,"duration":600}
```

### Dashboard card (Lovelace)

```yaml
type: entities
title: "Environmental station"
entities:
  - entity: sensor.sharp_sen66_displej_teplota
  - entity: sensor.sharp_sen66_displej_vlhkost
  - type: divider
  - entity: sensor.sharp_sen66_displej_pm2_5
  - entity: sensor.sharp_sen66_displej_co2
  - entity: sensor.sharp_sen66_displej_voc_index
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Display stays blank | Check the SPI pins (6, 7, 3) and the 3.3 V supply |
| MQTT error `rc=5` | Wrong username or password - Mosquitto rejects anonymous connections |
| MQTT error `rc=-2` | Broker unreachable: check the IP address and that port 1883 is still enabled |
| SEN66 error | Check the I2C pins (10, 8) and the sensor cable |
| Wi-Fi FAILED | Check SSID and password, or reconnect through the captive portal |
| Entities missing in HA | Make sure the MQTT integration is set up with discovery enabled; listen to `homeassistant/sensor/#` |
| Nothing published for the first minute | Expected, that is `mqttWarmupDelay` |
| CO2 shows `---` | Normal for about the first minute after boot; if it persists, the value is outside the valid 300-10000 ppm range |
| Two devices overwrite each other's data | Give each one a different **base topic** in the web UI |
| Duplicate entities in HA after upgrading from 2.0.0 | Since 2.2.1 the firmware removes the old discovery (`homeassistant/sensor/sen66_*`) on connect; new entities have the MAC in their ID |
| Entities "unavailable" although the device runs | The sensor delivered no valid data for 30 s; the firmware re-initializes it after repeated errors - check the SEN66 cable |
| Forgotten web UI password | Flash `factory.bin` over USB (erases the configuration) and set the device up again |

## Dependencies

Downloaded automatically by PlatformIO, versions are pinned in `platformio.ini`:

- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO)
- [Adafruit SHARP Memory Display](https://github.com/adafruit/Adafruit_SHARP_Memory_Display)
- [Sensirion I2C SEN66](https://github.com/Sensirion/arduino-i2c-sen66)
- [Sensirion Core](https://github.com/Sensirion/arduino-core)
- [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7.x

The `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `Update` and `Wire` libraries are part of the ESP32 Arduino core.

## Project structure

```
src/
  main.cpp              dashboard, MQTT, web UI, OTA, TMEP
  config.h/.cpp         AppConfig structure, NVS storage, validation
  WifiProvisioning.*    client connection, fallback AP, captive DNS
platformio.ini          build configuration with pinned versions
CHANGELOG.md            change log (Czech)
README.cs.md            Czech version of this document
.github/workflows/      build on every push, release with binaries on v* tags
```

## License

MIT License - see [LICENSE](LICENSE).

## Author

**Vladimír Waas** - [@VladaWaas](https://github.com/VladaWaas)
