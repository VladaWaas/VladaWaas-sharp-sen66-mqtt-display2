# Sharp Memory LCD + SEN66 - monitor kvality vzduchu

[English](README.md) | **Česky**

Stanice pro měření kvality vnitřního vzduchu s displejem **Sharp Memory LCD** a environmentálním senzorem **Sensirion SEN66**, propojená s **Home Assistantem** přes MQTT.

Postaveno na **ESP32-C3-MINI1**, PlatformIO + framework Arduino.

Aktuální firmware: **2.2.1** - viz [CHANGELOG.md](CHANGELOG.md).

![Sestava se SEN66](SEN66-asm2%20v11.png)

## Tištěná krabička

Krabička pro 3D tisk je ke stažení na Printables:

**[Case for SEN-66 with Sharp Memory Display](https://www.printables.com/model/1600651-case-for-sen-66-with-sharp-memory-display)**

## Co to umí

### Senzor Sensirion SEN66

- **Prachové částice**: PM1.0, PM2.5, PM4.0, PM10 (µg/m³)
- **Teplota** (°C) a **vlhkost** (%) s nastavitelným kalibračním offsetem
- **VOC Index** a **NOx Index**
- **CO2** (ppm) - validuje se zvlášť, protože náběh trvá zhruba minutu

### Displej Sharp LS027B7DH01 (400×240)

- Dashboard se všemi naměřenými hodnotami v reálném čase
- Hodnocení kvality vzduchu s vizuálním ukazatelem podle PM2.5
- Stavový řádek se stavem Wi-Fi, TMEP, MQTT a senzoru včetně doby běhu
- Bezpatkové fonty U8g2 v nativních velikostech, česká diakritika a správné symboly jednotek
- Teplota, vlhkost a CO₂ mají velké číslice vysoké 34 px; na šířku jsou vedle sebe, na výšku je CO₂ pod teplotou a vlhkostí. Prachové hodnoty, VOC a NOx zůstávají v kompaktní spodní části.
- Inverzní upozornění „CO₂ · vyvětrat“ od 2 000 ppm; nepřipravené plynové hodnoty zobrazené pomlčkou
- Rozložení pro otočení na šířku i na výšku, stejná typografie při startu a OTA aktualizaci
- Vzdálené vykreslování textu a grafiky přes MQTT
- Softwarová inverze barev (panel nemá hardwarovou inverzi)

Fonty jsou uložené přímo ve firmwaru, bez dalších souborů na zařízení.
Jejich zdroje, licence a postup opětovného generování jsou v
[src/fonts/README.md](src/fonts/README.md). Hostitelské vykreslovací testy lze
po `pio pkg install` spustit přes `python scripts/test_display.py`; vyžadují
kompilátor C++ (GCC, Clang nebo MSVC). Náhledy vzniknou v `.pio/display-tests/`.
S nainstalovaným Pillow se kromě PBM vytvoří i PNG.

### Integrace do Home Assistantu

- **MQTT auto-discovery** - senzory se objeví automaticky jako jedno zařízení
- Samostatné topicy pro každou veličinu plus souhrnný JSON topic
- Dostupnost online/offline přes Last Will and Testament
- `state_class: measurement`, takže Home Assistant vede dlouhodobé statistiky
- Discovery se odešle znovu po restartu Home Assistantu (zařízení odebírá birth message)
- `unique_id` i MQTT client ID obsahují MAC adresu, takže vedle sebe může běžet víc kusů
- Obousměrné řízení - z Home Assistantu lze na displej poslat zprávu

### Konfigurace

- Zprovoznění Wi-Fi přes **AP a captive portál** (ve zdrojácích nejsou žádné přihlašovací údaje)
- Vestavěné webové rozhraní na portu 80, volitelně chráněné heslem
- **Aktualizace firmware přes web (OTA)** - stačí nahrát `firmware.bin` z GitHub release
- Veškeré nastavení se ukládá do NVS
- Volitelné odesílání na **TMEP.cz** přes HTTP GET se šablonovanými parametry

### Webový přehled aktuálních dat

- Responzivní karty s českými popisky, desetinnou čárkou a jednotkami °C, %, µg/m³ a ppm. VOC a NOₓ jsou bezrozměrné indexy, nikoli koncentrace.
- CO₂, PM2,5, PM10, VOC a NOₓ mají barevné i slovní hodnocení. Rozbalovací legenda uvádí přesné hranice a vysvětlení; jde o orientační stupnici okamžitých hodnot, nikoli zdravotní limity či výpočet AQI. PM1 a PM4 zůstávají bez barevného hodnocení.
- Obnova každé 2 sekundy, samostatné stavy připojení a doba běhu. Při neplatných datech, náběhu plynového senzoru nebo výpadku webového spojení se místo dotčených hodnot zobrazí pomlčka a vysvětlení.

## Hardware

| Součástka | Zapojení |
|-----------|----------|
| ESP32-C3-MINI1 | USB pro napájení a programování |
| Sharp LS027B7DH01 (2,7" 400×240) | SPI: CLK=GPIO6, MOSI=GPIO7, CS=GPIO3 |
| Sensirion SEN66 | I2C: SDA=GPIO10, SCL=GPIO8 (adresa 0x6B) |

### Přehled pinů

```
Pin ESP32-C3     Funkce          Periferie
───────────────────────────────────────────
GPIO6            SPI CLK         Sharp LCD
GPIO7            SPI MOSI        Sharp LCD
GPIO2            SPI MISO        (nevyužito, panel je jen pro zápis)
GPIO3            SPI CS          Sharp LCD
GPIO10           I2C SDA         SEN66
GPIO8            I2C SCL         SEN66
```

## Instalace

### Co budete potřebovat

- [VS Code](https://code.visualstudio.com/) s rozšířením [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)
- Desku s ESP32-C3 připojenou přes USB

### Postup

1. **Naklonujte repozitář**

   ```bash
   git clone https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2.git
   cd VladaWaas-sharp-sen66-mqtt-display2
   ```

2. **Otevřete projekt ve VS Code** - `File → Open Folder` a vyberte naklonovanou složku.

3. **Přeložte a nahrajte** - `Ctrl+Shift+P` → `PlatformIO: Upload`, případně šipkou **→** v modrém stavovém řádku.

4. **Sledujte sériový výstup** - `Ctrl+Shift+P` → `PlatformIO: Serial Monitor` (115200 baud).

Bez překládání lze použít hotové soubory z [GitHub Releases](https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2/releases) - viz [Aktualizace firmware](#aktualizace-firmware).

Ve zdrojových souborech není potřeba nic upravovat. Výchozí hodnoty jsou v `src/config.h` a všechno se nastavuje za běhu přes captive portál a webové rozhraní.

## První spuštění a captive portál

1. Po startu firmware načte uložené přihlašovací údaje k Wi-Fi z NVS.
2. Pokud žádné SSID uloženo není nebo se připojení do zhruba 20 sekund nepovede, zařízení přepne do režimu přístupového bodu.
3. Přístupový bod se jmenuje `SharpDisplay-<MAC>` (poslední tři bajty MAC adresy) a je **ve výchozím stavu otevřený**.
4. DNS server odpovídá na všechny domény adresou přístupového bodu (`192.168.4.1`) a HTTP požadavky přesměrovává na `/`.
5. Portál nabízí stejné webové rozhraní jako běžný provoz včetně sekce **Wi-Fi**:
   - uložení SSID a hesla (`/api/wifi/save`)
   - zapomenutí Wi-Fi (`/api/wifi/forget`)
6. Po uložení se zařízení restartuje a připojí se jako klient.

Stavy Wi-Fi: `WIFI_STA_CONNECTING` → `WIFI_STA_CONNECTED`, se záložním `WIFI_AP_CAPTIVE`.

### Výpadek Wi-Fi

Pokud se zařízení s uloženým SSID nepřipojí (např. při restartu routeru), přepne do záložního přístupového bodu, ale **každé 2 minuty zkouší původní síť znovu** a po jejím návratu se k ní samo připojí. Přístupový bod mezitím běží dál. Pokud je k němu právě někdo připojený, pokus se odloží, aby se mu nerozbilo rozdělané nastavení.

V záložním režimu je webové rozhraní **chráněné heslem** stejně jako v běžném provozu. Bez hesla je jen prvotní nastavení, tj. dokud zařízení nemá uložené žádné SSID.

## Webové rozhraní

Jakmile je zařízení ve vaší síti, otevřete `http://<ip-zarizeni>/`.

### Záložka Aktuální data

Živé hodnoty ze SEN66, stav Wi-Fi a MQTT, stav TMEP a doba běhu.

### Záložka Konfigurace

| Skupina | Položky |
|---------|---------|
| Wi-Fi | SSID, heslo (lze smazat pro otevřenou síť), uložení jen Wi-Fi, zapomenutí Wi-Fi |
| MQTT | server, port, uživatel, heslo, client ID, **base topic**, **discovery prefix Home Assistantu** |
| Zabezpečení web UI | uživatel a heslo pro HTTP Basic Auth (smazané heslo = bez přihlašování) |
| TMEP.cz | doména, parametry požadavku, náhled skutečné URL, ruční odeslání |
| Displej | rotace (0-3, výchozí **2**), inverze (0/1) |
| Intervaly | překreslení displeje, MQTT publish, TMEP request, MQTT warmup delay |
| Kalibrace | teplotní offset v °C (výchozí **−2,0**), přičítá se k naměřené hodnotě |

Konfigurace se zapisuje do NVS a přežije restart. Uložení zařízení restartuje.

> **Hesla API nikdy neposílá zpět.** `GET /api/config` vrací pouze příznaky
> `wifiPasswordSet` / `mqttPasswordSet` / `webPasswordSet` (formulář podle nich ukáže
> „nastaveno“ / „nenastaveno“). Ponechání pole s heslem prázdného znamená, že se uložená
> hodnota nemění. Heslo se odstraní zaškrtnutím **Smazat heslo** u příslušného pole
> (v API odesláním prázdného řetězce).

> Rozvržení dashboardu počítá s rotacemi na šířku (0 a 2). Rotace 1 a 3 panel sice
> otočí, ale layout pro 240×400 připravený není.

### Záložka Aktualizace

Nahrání nového firmware přes síť - viz [Aktualizace firmware](#aktualizace-firmware).

## Aktualizace firmware

Každé vydání na [GitHub Releases](https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2/releases) obsahuje dva hotové soubory, které sestavuje GitHub Actions:

| Soubor | K čemu | Konfigurace |
|--------|--------|-------------|
| `firmware.bin` | Aktualizace přes web (OTA) | zůstane zachována |
| `factory.bin` | Čistá instalace kabelem od adresy `0x0` | **smaže se** (Wi-Fi, MQTT, hesla) |

### Přes web (OTA) - od verze 2.2.1

1. Stáhněte `firmware.bin` z požadovaného vydání.
2. Ve webovém rozhraní otevřete záložku **Aktualizace**, vyberte soubor a dejte **Nahrát a restartovat**.
3. Zařízení firmware ověří, restartuje se a za zhruba 20 sekund naběhne s novou verzí.

OTA funguje jen tehdy, když je zařízení připojené k Wi-Fi (v záložním přístupovém bodu je vypnutá) a je chráněné stejným heslem jako webové rozhraní. Poškozený nebo nesprávný soubor zařízení odmítne a běží dál na původní verzi.

### Kabelem

Starší verze než 2.2.1 OTA neumí, takže na ni je potřeba jednou přejít kabelem:

- **se zachováním konfigurace**: z naklonovaného repozitáře `PlatformIO: Upload` (viz [Instalace](#instalace)),
- **načisto**: `factory.bin` od adresy `0x0`, např. přes webový [ESP Tool](https://espressif.github.io/esptool-js/) v Chrome/Edge. Hodí se i při zapomenutém hesle k webovému rozhraní.

## Nastavení Home Assistantu

Firmware potřebuje MQTT brokera. Home Assistant **žádného neobsahuje** - instaluje se zvlášť. Následující postup platí pro běžnou instalaci Home Assistant OS nebo Supervised.

### 1. Instalace Mosquitto brokera

1. Otevřete **Nastavení → Doplňky → Obchod s doplňky** (v novějších verzích **Nastavení → Aplikace → Instalovat aplikaci**).
2. Najděte **Mosquitto broker** - ten oficiální, spravovaný týmem Home Assistantu - a dejte **Instalovat**.
3. Zapněte **Spustit při startu** a **Watchdog**, pak doplněk **spusťte**.
4. Zkontrolujte log doplňku; čistý start znamená, že broker poslouchá.

Výchozí konfigurace nepotřebuje žádné úpravy. Porty: **1883** nešifrované MQTT, 1884 websockety. Pokud v nastavení doplňku vyplníte `certfile` a `keyfile`, přibudou porty 8883 a 8884 pro TLS - nešifrované porty zůstávají funkční, dokud je ručně nevymažete v síťové kartě doplňku.

### 2. Vytvoření vyhrazeného MQTT uživatele

Doplněk ověřuje přihlášení proti uživatelskému systému Home Assistantu, takže pro zařízení vytvořte běžného uživatele:

1. **Nastavení → Osoby → Uživatelé → Přidat uživatele.**
2. Zvolte jméno jako `mqtt-senzory` a silné heslo.

Dvě důležitá omezení:

- Uživatel **se nesmí jmenovat `homeassistant` ani `addons`** - obě jména jsou vyhrazená pro vnitřní ekosystém.
- Pokud záložku *Uživatelé* nevidíte, zapněte si ve svém profilu **Pokročilý režim**.

Doplněk **nepodporuje anonymní připojení** - `allow_anonymous true` nefunguje, takže uživatelské jméno a heslo jsou povinné.

### 3. Přidání integrace MQTT

1. Jděte do **Nastavení → Zařízení a služby**.
2. MQTT se obvykle sám objeví nahoře mezi nalezenými integracemi. Pokud ne, použijte **Přidat integraci → MQTT**.
3. Potvrďte, že má Home Assistant použít brokera z doplňku Mosquitto, a zkontrolujte, že je zaškrtnuté **povolení discovery**.

Home Assistant si vlastní přihlašovací údaje k brokerovi vygeneruje a spravuje sám - uživatel z kroku 2 je pro ESP32, ne pro Home Assistant.

### 4. Nasměrování zařízení na brokera

Ve webovém rozhraní zařízení, **Konfigurace → MQTT**:

| Pole | Hodnota |
|------|---------|
| Server | **IP adresa počítače s Home Assistantem v lokální síti**, např. `192.168.1.10` |
| Port | `1883` |
| Uživatel | uživatel z kroku 2, např. `mqtt-senzory` |
| Heslo | heslo tohoto uživatele |
| Base topic | `sharp` (změňte, pokud provozujete víc kusů) |
| Discovery prefix | `homeassistant` (měňte jen tehdy, když jste ho změnili v integraci MQTT) |

Zadejte IP adresu, ne `core-mosquitto` - tento hostname se překládá pouze uvnitř interní Docker sítě Home Assistantu a z ESP32 na něj nedosáhnete.

Uložte. Zařízení se restartuje, připojí se a odešle retained discovery zprávy. Během několika sekund se pod integrací MQTT objeví nové zařízení **Sharp SEN66 Displej** s devíti entitami.

### 5. Ověření

Pokud se entity neobjeví:

1. **Nastavení → Zařízení a služby → MQTT → Konfigurovat.**
2. Do pole **Poslouchat téma** zadejte `sharp/#` (nebo `#` pro všechno) a dejte **Začít poslouchat**.
3. Měly by přicházet hodnoty ze senzoru a na `sharp/status` hodnota `online`.
4. Poslechněte si i `homeassistant/sensor/#` a ověřte, že dorazily discovery zprávy.

Druhou polovinu obrázku dá sériový monitor - `MQTT: CHYBA rc=5` znamená špatné přihlašovací údaje, `rc=-2` nedostupného brokera.

Počítejte s tím, že zařízení po prvním platném vzorku čeká **60 sekund** (`mqttWarmupDelay`), než začne cokoli publikovat. Hned po restartu se tedy nic neobjeví. Toto zdržení tam je proto, aby se do historie nedostaly nesmyslné hodnoty z náběhu senzoru.

### Volitelně: omezení topiců přes ACL

Pokud chcete účet zařízení omezit jen na jeho vlastní topicy, zapněte v doplňku volbu `customize` a přidejte ACL soubor. Home Assistant očekává, že uživatelé `homeassistant` a `addons` si ponechají neomezený zápis i čtení všech topiců - jejich vynechání vám instalaci rozbije. Přesnou podobu souborů popisuje dokumentace doplňku.

## Odesílání na TMEP.cz

Zařízení umí každý vzorek volitelně poslat na TMEP.cz přes HTTP GET.

Pole ve webovém rozhraní:

- **Doména pro zasílání hodnot** - např. `xxk4sk-g6rxfh` (požadavek jde na `http://<domena>.tmep.cz/`)
- **Parametry požadavku** - např. `tempV=*TEMP*&humV=*HUM*&rssi=*RSSI*`

Podporované proměnné (fungují tvary `*NAZEV*` i `{NAZEV}`):

| Ze senzoru | Systémové |
|------------|-----------|
| `*TEMP*`, `*HUM*` | `*RSSI*` |
| `*PM1*`, `*PM2*` (PM2.5), `*PM4*`, `*PM10*` | `*UPTIME*` (sekundy) |
| `*VOC*`, `*NOX*` | `*FREEHEAP*` (bajty) |
| `*CO2*` | `*IP*` |

`*CO2*` se nahradí prázdným řetězcem, dokud CO2 kanál nenaběhne.

## MQTT topicy

Všechny topicy níže vycházejí z nastavitelného base topicu, ve výchozím stavu `sharp`.

### Odebírané (příchozí, řízení displeje)

| Topic | Payload | Popis |
|-------|---------|-------|
| `sharp/display/text` | `"Ahoj!"` | Zobrazí text na 30 sekund |
| `sharp/display/clear` | `""` | Smaže displej |
| `sharp/display/command` | JSON | Pokročilé příkazy (viz níže) |
| `homeassistant/status` | `online` | Birth message Home Assistantu, spouští znovuodeslání discovery |

### Publikované (odchozí, data ze senzoru)

| Topic | Příklad | Popis |
|-------|---------|-------|
| `sharp/sensor/temperature` | `23.4` | Teplota °C |
| `sharp/sensor/humidity` | `45.2` | Vlhkost % |
| `sharp/sensor/pm1` | `5.2` | PM1.0 µg/m³ |
| `sharp/sensor/pm25` | `8.7` | PM2.5 µg/m³ |
| `sharp/sensor/pm4` | `9.1` | PM4.0 µg/m³ |
| `sharp/sensor/pm10` | `10.3` | PM10 µg/m³ |
| `sharp/sensor/voc` | `125` | VOC Index |
| `sharp/sensor/nox` | `12` | NOx Index |
| `sharp/sensor/co2` | `832` | CO2 ppm (dokud není platné, neodesílá se) |
| `sharp/sensor` | `{...}` | Všechny hodnoty jako JSON |
| `sharp/status` | `online` | Dostupnost (retained, LWT). `offline` i tehdy, když ze senzoru 30 s nepřišla platná data |

Všechny zprávy se senzorickými daty se publikují jako retained.

### JSON příkazy (`sharp/display/command`)

```jsonc
// Text s pozicí, velikostí a dobou zobrazení
{"text": "Ahoj!", "x": 50, "y": 100, "size": 4, "duration": 60}

// Čára
{"line": {"x1": 0, "y1": 120, "x2": 399, "y2": 120}}

// Obdélník (vyplněný nebo obrys)
{"rect": {"x": 10, "y": 10, "w": 100, "h": 50, "fill": true}}

// Inverze barev (softwarová, do NVS se neukládá)
{"invert": true}

// Návrat na dashboard se senzory
{"dashboard": true}
```

## Příklady pro Home Assistant

Identifikátory entit níže odpovídají tomu, co Home Assistant odvodí z discovery zpráv. Ty své si ověřte v **Nastavení → Zařízení a služby → MQTT**.

### Poslat na displej upozornění

```yaml
automation:
  - alias: "Displej - upozornění na otevřené okno"
    triggers:
      - trigger: state
        entity_id: binary_sensor.okno
        to: "on"
    actions:
      - action: mqtt.publish
        data:
          topic: "sharp/display/command"
          payload: >
            {"text":"OTEVRENE OKNO!","x":30,"y":80,"size":3,"duration":120}
```

### Upozornění na zhoršenou kvalitu vzduchu

```yaml
automation:
  - alias: "Výstraha - špatná kvalita vzduchu"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.sharp_sen66_displej_pm2_5
        above: 35
        for:
          minutes: 5
    actions:
      - action: notify.persistent_notification
        data:
          title: "Varování ke kvalitě vzduchu"
          message: "PM2.5 je {{ states('sensor.sharp_sen66_displej_pm2_5') }} µg/m³"
```

### Připomínka větrání při vysokém CO2

```yaml
automation:
  - alias: "Větrání - vysoké CO2"
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
            {"text":"VETRAT!\nCO2: {{ states('sensor.sharp_sen66_displej_co2') }} ppm","x":30,"y":70,"size":3,"duration":600}
```

### Karta na dashboard (Lovelace)

```yaml
type: entities
title: "Environmentální stanice"
entities:
  - entity: sensor.sharp_sen66_displej_teplota
  - entity: sensor.sharp_sen66_displej_vlhkost
  - type: divider
  - entity: sensor.sharp_sen66_displej_pm2_5
  - entity: sensor.sharp_sen66_displej_co2
  - entity: sensor.sharp_sen66_displej_voc_index
```

## Řešení potíží

| Problém | Řešení |
|---------|--------|
| Displej zůstává prázdný | Zkontrolujte SPI piny (6, 7, 3) a napájení 3,3 V |
| MQTT chyba `rc=5` | Špatné jméno nebo heslo - Mosquitto anonymní připojení odmítá |
| MQTT chyba `rc=-2` | Broker není dostupný: zkontrolujte IP adresu a že port 1883 je stále povolený |
| Chyba SEN66 | Zkontrolujte I2C piny (10, 8) a kabel k senzoru |
| Wi-Fi FAILED | Zkontrolujte SSID a heslo, případně připojte znovu přes captive portál |
| V HA chybí entity | Ověřte, že je nastavená integrace MQTT a povolené discovery; poslechněte si `homeassistant/sensor/#` |
| První minutu se nic nepublikuje | Tak to má být, jde o `mqttWarmupDelay` |
| CO2 ukazuje `---` | Zhruba první minutu po startu normální; pokud to trvá, hodnota je mimo platné rozmezí 300-10000 ppm |
| Dvě zařízení si přepisují data | Nastavte každému jiný **base topic** ve webovém rozhraní |
| Po přechodu z 2.0.0 má HA entity dvakrát | Od 2.2.1 firmware staré discovery (`homeassistant/sensor/sen66_*`) při připojení sám smaže; nové entity mají v ID MAC adresu |
| Entity jsou „nedostupné“, i když zařízení běží | Senzor 30 s nedodal platná data; firmware ho po opakovaných chybách sám zkusí znovu inicializovat - zkontrolujte kabel k SEN66 |
| Zapomenuté heslo k webu | Nahrajte kabelem `factory.bin` (smaže konfiguraci) a zařízení nastavte znovu |

## Závislosti

Stáhne je automaticky PlatformIO, verze jsou připnuté v `platformio.ini`:

- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO)
- [Adafruit SHARP Memory Display](https://github.com/adafruit/Adafruit_SHARP_Memory_Display)
- [Sensirion I2C SEN66](https://github.com/Sensirion/arduino-i2c-sen66)
- [Sensirion Core](https://github.com/Sensirion/arduino-core)
- [PubSubClient](https://github.com/knolleary/pubsubclient) (MQTT)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7.x

Knihovny `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `Update` a `Wire` jsou součástí ESP32 Arduino core.

## Struktura projektu

```
src/
  main.cpp              dashboard, MQTT, webové rozhraní, OTA, TMEP
  config.h/.cpp         struktura AppConfig, ukládání do NVS, validace
  WifiProvisioning.*    připojení jako klient, záložní AP, captive DNS
platformio.ini          konfigurace buildu s připnutými verzemi
CHANGELOG.md            přehled změn
README.md               anglická verze tohoto dokumentu
.github/workflows/      build při každém pushi, při tagu v* vydání s binárkami
```

## Licence

MIT License - viz [LICENSE](LICENSE).

## Autor

**Vladimír Waas** - [@VladaWaas](https://github.com/VladaWaas)
