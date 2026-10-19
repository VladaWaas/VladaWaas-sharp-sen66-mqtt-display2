#include "DisplayDashboard.h"
#include "fonts/DisplayFonts.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace dashboard {
namespace {
struct Painter {
  Adafruit_GFX& display;
  U8G2_FOR_ADAFRUIT_GFX font;
  uint16_t ink, paper;

  Painter(Adafruit_GFX& target, uint16_t foreground, uint16_t background)
      : display(target), ink(foreground), paper(background) {
    font.begin(display);
    font.setFontMode(1);  // Transparent text; the whole frame is cleared first.
    font.setFontDirection(0);
    display.fillRect(0, 0, display.width(), display.height(), paper);
  }

  int width(const char* text, const uint8_t* face) {
    font.setFont(face);
    return font.getUTF8Width(text);
  }

  int text(int x, int baseline, const char* value, const uint8_t* face = sen66_helvR08,
           bool inverse = false, int maxWidth = -1) {
    font.setFont(face);
    font.setForegroundColor(inverse ? paper : ink);
    font.setBackgroundColor(inverse ? ink : paper);
    if (maxWidth < 0) maxWidth = display.width() - x - 8;
    char fitted[160];
    snprintf(fitted, sizeof(fitted), "%s", value);
    // Truncate at a UTF-8 boundary. The two dots make omitted diagnostics visible.
    if (font.getUTF8Width(fitted) > maxWidth) {
      size_t length = strlen(fitted);
      do {
        if (!length) break;
        do { --length; } while (length && (static_cast<unsigned char>(fitted[length]) & 0xc0) == 0x80);
        fitted[length] = '\0';
        char candidate[164];
        snprintf(candidate, sizeof(candidate), "%s..", fitted);
        if (font.getUTF8Width(candidate) <= maxWidth) {
          snprintf(fitted, sizeof(fitted), "%s", candidate);
          break;
        }
      } while (length);
    }
    return x + font.drawUTF8(x, baseline, fitted);
  }

  void centered(int baseline, const char* value, const uint8_t* face) {
    const int w = width(value, face);
    text(w < display.width() - 16 ? (display.width() - w) / 2 : 8, baseline, value, face);
  }

  void centeredIn(int x, int fieldWidth, int baseline, const char* value,
                  const uint8_t* face, bool inverse = false) {
    const int w = width(value, face);
    text(x + (fieldWidth - w) / 2, baseline, value, face, inverse, fieldWidth - 8);
  }

  void primary(int x, int fieldWidth, int baseline, float value, bool ready,
               int decimals, const char* unit, bool inverse = false) {
    char buffer[24];
    const bool available = ready && isfinite(value);
    if (!available) snprintf(buffer, sizeof(buffer), "–");
    else {
      snprintf(buffer, sizeof(buffer), decimals ? "%.1f" : "%.0f", value);
      for (char* p = buffer; *p; ++p) if (*p == '.') *p = ',';
    }
    // The numeric font has no en dash; keep the warmup placeholder in Helvetica.
    centeredIn(x, fieldWidth, baseline, buffer,
               available ? u8g2_font_logisoso34_tn : sen66_helvB24, inverse);
    centeredIn(x, fieldWidth, baseline + 17, unit, sen66_helvR08, inverse);
  }

  void divider(int y) { display.drawFastHLine(8, y, display.width() - 16, ink); }

  void number(int x, int baseline, float value, bool ready, int decimals,
              const uint8_t* face, const char* unit, bool inverse = false,
              bool unitBelow = false) {
    char buffer[24];
    if (!ready || !isfinite(value)) snprintf(buffer, sizeof(buffer), "–");
    else {
      snprintf(buffer, sizeof(buffer), decimals ? "%.1f" : "%.0f", value);
      for (char* p = buffer; *p; ++p) if (*p == '.') *p = ',';
    }
    const int end = text(x, baseline, buffer, face, inverse);
    if (unit && *unit) text(unitBelow ? x : end + 8, unitBelow ? baseline + 17 : baseline,
                            unit, sen66_helvR08, inverse);
  }
};

void status(Painter& p, const Data& d, bool portrait) {
  char uptime[24];
  const unsigned long hours = d.uptimeSeconds / 3600;
  const unsigned long minutes = d.uptimeSeconds % 3600 / 60;
  // Bounded even at millis() rollover: up to 1193 h 02 min.
  snprintf(uptime, sizeof(uptime), "%lu h %02lu min", hours, minutes);
  const int uptimeX = p.display.width() - 9 - p.width(uptime, sen66_helvR08);
  p.text(8, 14, d.wifi, sen66_helvR08, false, portrait ? uptimeX - 16 : 117);
  p.text(uptimeX, 14, uptime);
  const int baseline = portrait ? 31 : 14;
  p.text(portrait ? 8 : 133, baseline, d.mqttConnected ? "MQTT OK" : "MQTT !",
         sen66_helvR08, false, 60);
  char tmep[24];
  const char* result = strncmp(d.tmep, "TMEP:", 5) == 0 ? d.tmep + 5 : d.tmep;
  snprintf(tmep, sizeof(tmep), "TMEP %s", result);
  p.text(portrait ? 86 : 201, baseline, tmep, sen66_helvR08, false, 60);
  p.text(portrait ? 166 : 266, baseline, !d.sensorReady || d.stale ? "SEN !" : d.valid ? "SEN OK" : "SEN čeká",
         sen66_helvR08, false, portrait ? 65 : uptimeX - 274);
  p.divider(portrait ? 40 : 22);
}

void footer(Painter& p, const Data& d, bool portrait) {
  static const char* const labels[] = {"Výborný", "Dobrý", "Přijatelný", "Špatný", "Velmi špatný", "Nebezpečný"};
  const uint8_t band = airQualityBand(d.pm[1]);
  p.divider(portrait ? 349 : 214);
  p.text(12, portrait ? 365 : 232, "Vzduch · PM2,5");
  p.text(105, portrait ? 365 : 232, labels[band], sen66_helvB08,
         false, portrait ? 127 : 164);
  for (int i = 0; i < 6; ++i) {
    const int x = (portrait ? 12 : 280) + i * (portrait ? 36 : 19);
    const int y = portrait ? 382 : 224;
    const int w = portrait ? 29 : 16;
    if (i == band) p.display.fillRect(x, y, w, 8, p.ink);
    else p.display.drawRect(x, y, w, 8, p.ink);
  }
}
}  // namespace

uint8_t airQualityBand(float pm25) {
  static const float thresholds[] = {12.0f, 35.4f, 55.4f, 150.4f, 250.4f};
  for (uint8_t i = 0; i < 5; ++i) if (pm25 < thresholds[i]) return i;
  return 5;
}

void draw(Adafruit_GFX& display, const Data& d, uint16_t ink, uint16_t paper) {
  Painter p(display, ink, paper);
  const bool portrait = display.width() < display.height();
  status(p, d, portrait);
  if (!d.valid) {
    const int middle = display.height() / 2;
    p.centered(middle - 10, d.stale ? "Data nejsou aktuální" : "Čekám na data", sen66_helvB14);
    p.centered(middle + 16, d.sensorReady ? "ze senzoru SEN66..." : "Senzor není dostupný", sen66_helvR08);
    return;
  }

  const int mainBaseline = portrait ? 99 : 84;
  const int fieldWidth = portrait ? 112 : 126;
  const int firstX = portrait ? 4 : 5;
  const int secondX = portrait ? 124 : 138;
  p.centeredIn(firstX, fieldWidth, portrait ? 56 : 40, "Teplota", sen66_helvB08);
  p.centeredIn(secondX, fieldWidth, portrait ? 56 : 40, "Vlhkost", sen66_helvB08);
  p.primary(firstX, fieldWidth, mainBaseline, d.temperature, true, 1, "°C");
  p.primary(secondX, fieldWidth, mainBaseline, d.humidity, true, 1, "%");
  display.drawFastVLine(portrait ? 119 : 133, portrait ? 49 : 32, portrait ? 68 : 70, ink);

  const bool alert = d.co2Valid && d.co2 >= 2000;
  if (portrait) p.divider(126);
  else display.drawFastVLine(266, 32, 70, ink);
  if (alert) display.fillRect(portrait ? 8 : 270, portrait ? 133 : 28,
                              portrait ? 224 : 122, portrait ? 73 : 77, ink);
  const int co2X = portrait ? 8 : 271;
  const int co2Width = portrait ? 224 : 120;
  p.centeredIn(co2X, co2Width, portrait ? 147 : 40,
               alert ? "CO₂ · vyvětrat" : "CO₂", sen66_helvB08, alert);
  p.primary(co2X, co2Width, portrait ? 184 : 84, d.co2, d.co2Valid, 0, "ppm", alert);
  p.divider(portrait ? 211 : 112);

  static const char* const pmLabels[] = {"PM1", "PM2,5", "PM4", "PM10"};
  for (int i = 0; i < 4; ++i) {
    const int x = 12 + (portrait ? i % 2 * 120 : i * 100);
    const int offset = portrait ? i / 2 * 47 : 0;
    // Leave the second-row labels below the portrait divider, not across it.
    p.text(x, portrait ? 226 + i / 2 * 50 : 130, pmLabels[i], sen66_helvB08);
    p.number(x - 1, (portrait ? 247 : 153) + offset, d.pm[i], true, 1, sen66_helvB14, "");
    p.text(x, (portrait ? 261 : 170) + offset, "µg/m³");
  }
  if (portrait) p.divider(267);
  p.divider(portrait ? 315 : 180);

  for (int i = 0; i < 2; ++i) {
    const int x = 12 + i * (portrait ? 120 : 200);
    p.text(x, portrait ? 337 : 202, i ? "NOx" : "VOC", sen66_helvB08);
    const float index = i ? d.nox : d.voc;
    p.number(x + (i ? 35 : 33), portrait ? 337 : 202, index, index >= 1, 0,
             sen66_helvB14, "index");
  }
  footer(p, d, portrait);
}

void message(Adafruit_GFX& display, const char* title, const char* const* lines,
             uint8_t count, uint16_t ink, uint16_t paper) {
  Painter p(display, ink, paper);
  const int top = (display.height() - 40 - count * 22) / 2;
  p.centered(top + 24, title, display.width() < 300 ? sen66_helvB14 : sen66_helvB18);
  p.divider(top + 38);
  for (uint8_t i = 0; i < count; ++i) p.centered(top + 60 + i * 22, lines[i], sen66_helvR08);
}
}  // namespace dashboard
