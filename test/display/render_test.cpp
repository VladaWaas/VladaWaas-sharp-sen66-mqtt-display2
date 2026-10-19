#include "DisplayDashboard.h"
#include "fonts/DisplayFonts.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits>

int main() {
  dashboard::Data d={24.2f,38.7f,{2.6f,5.6f,8.0f,9.2f},96,1,991,
                     true,true,true,false,true,"Wi-Fi 192.168.1.66","TMEP:OK",10680};
  // Every static glyph, including Czech and physical units, must exist in each
  // subset. UTF-8 support alone does not establish font coverage.
  for(const uint8_t* face:{sen66_helvR08,sen66_helvB08,sen66_helvB14,sen66_helvB18,sen66_helvB24}) {
    Adafruit_GFX target(400,240);
    U8G2_FOR_ADAFRUIT_GFX font; font.begin(target);font.setFont(face);
    const char* required="Příliš žluťoučký kůň úpěl ďábelské ódy. Čekám µg/m³ °C CO₂ –";
    for(const unsigned char* s=reinterpret_cast<const unsigned char*>(required);*s;++s) {
      uint16_t c=font.utf8_next(*s);
      if(c<0xfffe)assert(u8g2_IsGlyph(&font.u8g2,c));
    }
  }
  const float thresholds[]={12,35.4f,55.4f,150.4f,250.4f};
  {
    Adafruit_GFX target(400,240);
    U8G2_FOR_ADAFRUIT_GFX font; font.begin(target);font.setFont(u8g2_font_logisoso34_tn);
    assert(font.getFontAscent()==34);
    for(const char* s="0123456789,-";*s;++s)assert(u8g2_IsGlyph(&font.u8g2,*s));
    // Reserve margins in all three landscape fields and portrait half-widths.
    for(const char* s:{"-60,0","105,0","100,0"})assert(font.getUTF8Width(s)<=104);
    assert(font.getUTF8Width("10000")<=112);
  }
  for(int i=0;i<5;++i) {
    assert(dashboard::airQualityBand(thresholds[i]-.01f)==i);
    assert(dashboard::airQualityBand(thresholds[i])==i+1);
  }
  Adafruit_GFX normal(400,240), inverse(400,240);
  dashboard::draw(normal,d,0,1);
  dashboard::draw(inverse,d,1,0);
  normal.save("normal.pbm");
  for(size_t i=0;i<normal.pixels.size();++i)assert(normal.pixels[i]==!inverse.pixels[i]);
  d.co2=2200;
  dashboard::draw(normal,d,0,1);normal.save("co2-alert.pbm");
  assert(normal.pixels[29*400+271]==0);
  d.co2=1999;dashboard::draw(normal,d,0,1);
  assert(normal.pixels[29*400+271]==1);
  d.co2=2000;dashboard::draw(normal,d,0,1);
  assert(normal.pixels[29*400+271]==0);
  dashboard::draw(inverse,d,1,0);
  for(size_t i=0;i<normal.pixels.size();++i)assert(normal.pixels[i]==!inverse.pixels[i]);
  d.co2Valid=false;dashboard::draw(normal,d,0,1);
  assert(normal.pixels[29*400+271]==1);
  d.voc=d.nox=0;dashboard::draw(normal,d,0,1);normal.save("warmup.pbm");
  for(int width:{400,240})for(bool invert:{false,true}) {
    Adafruit_GFX target(width,width==400?240:400);
    d.valid=true;d.co2Valid=true;d.co2=10000;
    d.sensorReady=true;d.stale=false;d.mqttConnected=true;
    d.temperature=-60;d.humidity=100;
    d.voc=d.nox=500;for(float& pm:d.pm)pm=1000;
    d.uptimeSeconds=4294967;d.wifi="Wi-Fi 255.255.255.255";d.tmep="TMEP:WAIT";
    dashboard::draw(target,d,invert?1:0,invert?0:1);
    if(!invert)target.save(width==400?"extremes.pbm":"portrait.pbm");
    d.valid=false;d.stale=true;d.sensorReady=false;
    d.mqttConnected=false;d.wifi="AP 192.168.4.1";d.tmep="TMEP:ERR";
    dashboard::draw(target,d,invert?1:0,invert?0:1);
    if(width==400&&!invert)target.save("unavailable.pbm");
    const char* lines[]={"FW: 2.2.1","Wi-Fi: Příliš dlouhá síť s diakritikou a dalšími znaky", "MQTT: 255.255.255.255", "Inicializace..."};
    dashboard::message(target,"Sharp LCD + SEN66",lines,4,invert?1:0,invert?0:1);
    const char* ota[]={"Nahráno. Restartuji zařízení..."};
    dashboard::message(target,"Aktualizace FW",ota,1,invert?1:0,invert?0:1);
    if(width==400&&!invert)target.save("ota.pbm");
  }
  puts("PASS: real U8g2 rendering, Czech glyphs, PM bands, CO2 alert boundaries, inversion, warmup, extremes, landscape/portrait, errors, splash and OTA; no out-of-bounds pixels.");
}
