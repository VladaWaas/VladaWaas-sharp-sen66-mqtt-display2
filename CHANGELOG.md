# Changelog

Formát vychází z [Keep a Changelog](https://keepachangelog.com/cs/1.1.0/).

## [Unreleased]

### Změněno

- Teplota, vlhkost a CO₂ mají přednost: velké číslice U8g2 Logisoso s výškou
  34 px ve třech horních polích. Na výšku má CO₂ vlastní velké pole pod teplotou
  a vlhkostí. PM, VOC a NOx zůstávají ve spodní části; zvýraznění CO₂, jednotky,
  desetinná místa a původní hranice hodnocení zůstávají zachovány.
- Fyzický displej: nové rozložení s fonty U8g2 v nativních velikostech,
  českou diakritikou, symboly jednotek a desetinnou čárkou. Prachové hodnoty
  mají jedno desetinné místo, VOC a NOx při náběhu pomlčku. CO₂ od 2 000 ppm
  zvýrazní inverzní pole s doporučením vyvětrat. Rozložení podporuje i otočení
  na výšku a softwarovou inverzi; startovací a OTA obrazovky používají stejné fonty.
- Hostitelské testy skutečného vykreslování U8g2 ověřují diakritiku, mezní hodnoty,
  hranice hodnocení, inverzi, náběh a chyby senzoru včetně generování náhledů.
- Webový přehled „Aktuální data“: responzivní karty s českými názvy, jednotkami,
  desetinnou čárkou a barevným i slovním hodnocením CO₂, PM2,5, PM10, VOC a NOₓ.
  Rozbalovací legenda vysvětluje orientační hranice a bezrozměrné plynové indexy.
- Přehledné stavy Wi-Fi, MQTT a TMEP, čas posledního spojení a čitelná doba běhu.
  Neplatné a nedostupné hodnoty se nezobrazují jako aktuální; webové požadavky mají
  timeout a při přerušeném spojení se obnovování automaticky opakuje.

## [2.2.1] - 2026-10-19

Verze 2.2.0 nebyla samostatně vydána, toto vydání obsahuje i všechny její změny - viz
sekce [2.2.0] v [CHANGELOG.md](https://github.com/VladaWaas/VladaWaas-sharp-sen66-mqtt-display2/blob/main/CHANGELOG.md).
Mimo jiné: zabezpečení webu heslem, MAC v `unique_id` (více zařízení v jednom HA),
`state_class` pro dlouhodobé statistiky, konfigurovatelný base topic a oprava ukládání
číselných hodnot z webového formuláře.

### Přidáno

- **Aktualizace firmware přes web (OTA)** - nová záložka **Aktualizace** ve webovém
  rozhraní, nahrává se `firmware.bin` z GitHub release. Chráněno heslem webového
  rozhraní, v záložním AP režimu vypnuto, `factory.bin` je odmítnut. Průběh i výsledek
  se ukazuje na displeji.
- **Vydání s binárkami** - workflow při tagu `v*` sestaví firmware, ověří, že se vejde
  do OTA oddílu, a k GitHub release přiloží `firmware.bin` (OTA) a `factory.bin`
  (čistá instalace kabelem od `0x0`).
- **Anglické README** - `README.md` je nově anglicky, česká verze je v `README.cs.md`.
- **Mazání hesel ve webovém rozhraní** - u hesla k Wi-Fi, MQTT i webu je volba
  *Smazat heslo*. Formulář navíc ukazuje, zda je heslo nastavené.

### Opraveno

- **Po výpadku Wi-Fi delším než 20 s zůstalo zařízení natrvalo v AP režimu**
  (typicky po restartu routeru), a to s otevřenou sítí a webem bez hesla. Nyní každé
  2 minuty zkouší původní síť (AP mezitím běží dál) a samo se vrátí. Záložní AP režim
  je navíc chráněný heslem webu; bez hesla zůstává jen prvotní nastavení.
- **Heslo nešlo ve webovém rozhraní smazat** - prázdné pole znamenalo „beze změny“,
  takže jednou nastavené heslo k webu nebo MQTT nešlo odstranit.
- **Duplicitní entity v Home Assistantu po přechodu z 2.0.0** - staré retained discovery
  zprávy (`homeassistant/sensor/sen66_*/config`) se při připojení k MQTT smažou.
- **Zamrzlá data** - když SEN66 přestal odpovídat, posílaly se do MQTT a TMEP stále
  poslední hodnoty. Po 30 s bez platného vzorku jsou data neplatná, displej ukáže
  „Čekám na data“ a `sharp/status` přejde na `offline`, takže HA entity označí jako
  nedostupné. Po obnovení dat se znovu čeká warmup.
- **SEN66 se po neúspěšné inicializaci už nikdy nezkusil znovu** - nyní každých 30 s,
  a také po 10 chybách čtení za sebou.
- **Záporná nebo nečíselná hodnota v `/api/config`** se u intervalů přetočila na obří
  číslo a prošla validací (např. `-1` → publikace nikdy). Nyní je požadavek odmítnut.
- **TMEP ignoroval warmup delay** a posílal hodnoty z náběhu senzoru. Ruční odeslání
  z webu funguje i během warmupu.
- Clone URL v README ukazovala na fork místo tohoto repozitáře.

## [2.2.0] - 2026-08-11

### Opraveno

- **Webová konfigurace zahazovala všechny číselné hodnoty.** HTML formulář posílá
  hodnoty jako řetězce a `doc["mqttPort"] | default` v ArduinoJson 7 vrací default,
  protože `is<int>()` je na řetězci `false`. Neuložil se tak port, rotace, inverze,
  žádný z intervalů ani teplotní offset. Opraveno na obou stranách (převod na číslo
  v JS + tolerantní parser `readNumber()` v C++).
- **CO2 shazovalo celý vzorek.** Validace vracela `false` pro `co2 < 350`, takže se
  během náběhu senzoru zahazovala i teplota, vlhkost a PM. Validace je nově rozdělená
  na "core" hodnoty a CO2, které se maskuje samostatně (na displeji `---`,
  do MQTT/TMEP se neposílá).
- **Přetečení `millis()`** u vypršení display override (~49,7 dne) — nahrazeno
  znaménkovým porovnáním.
- **`serializeJson()` do pevných bufferů** (`char[1024]`) tiše ořezával JSON, pokud
  uživatel zadal delší TMEP parametry → rozbité web UI. Nyní se serializuje do `String`.
- **Merge konflikt v README** (`<<<<<<< ours` / `>>>>>>> theirs`) z PR #2.
- **Chybná clone URL v README** — ukazovala na neexistující repozitář.
- **Zdvojený soubor `gitignore`** (bez tečky) odstraněn.
- **Blokující 20s pokus o připojení uvnitř HTTP handleru** `/api/wifi/save` —
  fetch v prohlížeči mohl vypršet dřív. Nyní se odpoví okamžitě a zařízení se restartuje.
- `Adafruit_SHARP_Memory_Display` a `arduino-i2c-sen66` se tahaly z Gitu bez tagu,
  `platform = espressif32` bez verze — vše připnuto na ověřené verze
  (GFX 1.12.6, BusIO 1.17.4, SHARP Memory Display 1.1.4, SEN66 1.3.1,
  Sensirion Core 0.7.3, PubSubClient 2.8, ArduinoJson 7.4.2).

### Přidáno

- **Zabezpečení web UI** volitelným heslem (HTTP Basic Auth). V captive režimu
  je portál záměrně bez hesla, jinak by nešlo zařízení prvně nastavit.
- **Konfigurovatelný base topic** (`sharp`) a **discovery prefix** (`homeassistant`).
  Dvě zařízení už si tak nepřepisují topicy.
- **`unique_id` a `identifiers` v HA discovery obsahují MAC** — dvě zařízení v jedné
  instanci Home Assistantu se už nepřepíšou. Stejně tak MQTT client ID.
- **`state_class: measurement`** v discovery → HA vede dlouhodobé statistiky.
  Dále `suggested_display_precision` a blok `origin`.
- **Znovuodeslání discovery po restartu HA** — firmware odebírá `homeassistant/status`
  a na `online` publikuje discovery znovu (doporučený postup z dokumentace HA).
- **Softwarová inverze displeje.** Sharp Memory LCD nemá HW inverzi, dřív se jen
  vypsala hláška na Serial. Nyní se prohodí kresebné barvy; jde i přes
  `{"invert":true}` v MQTT příkazu.
- **TMEP placeholdery `*RSSI*`, `*UPTIME*`, `*FREEHEAP*`, `*IP*`** — README je
  dokumentoval, ale implementace chyběla.
- `rssi` v souhrnném JSON topicu, verze FW na splash screenu a v `/api/data`.
- GitHub Actions workflow pro kontrolu buildu.
- Sekce README o nastavení Mosquitto brokeru a MQTT integrace v Home Assistantu.

### Změněno

- `doc.containsKey()` (deprecated od ArduinoJson 7.2) nahrazeno za `doc["k"].is<T>()`.
- Hesla se z `/api/config` už neposílají zpět; vrací se jen příznak
  `wifiPasswordSet` / `mqttPasswordSet` / `webPasswordSet`. Prázdné pole ve formuláři
  znamená "ponechat beze změny".
- MQTT buffer zvětšen na 2048 B kvůli většímu discovery payloadu.

### Odstraněno

- Topic `sharp/display/brightness` — byl odebírán, ale neexistoval pro něj handler
  a Sharp Memory LCD nemá podsvícení.
- Nepoužívaná položka konfigurace `tmepBaseUrl` (ukládala se do NVS, nikdy se nečetla).
