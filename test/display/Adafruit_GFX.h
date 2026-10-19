// Host-only framebuffer backend. The real U8g2 decoder and dashboard are tested.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <string>

class Print {
 public:
  virtual size_t write(uint8_t) = 0;
};

class Adafruit_GFX {
  int16_t w_, h_;
 public:
  std::vector<uint8_t> pixels;
  Adafruit_GFX(int16_t w, int16_t h): w_(w), h_(h), pixels(w*h,1) {}
  int16_t width() const { return w_; }
  int16_t height() const { return h_; }
  void drawPixel(int16_t x, int16_t y, uint16_t color) {
    if(x<0||y<0||x>=w_||y>=h_)throw std::runtime_error("Drawing outside display: "+std::to_string(x)+","+std::to_string(y));
    pixels[y*w_+x] = static_cast<uint8_t>(color);
  }
  void drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c) { for(int i=0;i<w;++i)drawPixel(x+i,y,c); }
  void drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c) { for(int i=0;i<h;++i)drawPixel(x,y+i,c); }
  void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) { for(int i=0;i<h;++i)drawFastHLine(x,y+i,w,c); }
  void drawRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) {
    drawFastHLine(x,y,w,c);drawFastHLine(x,y+h-1,w,c);
    drawFastVLine(x,y,h,c);drawFastVLine(x+w-1,y,h,c);
  }
  void save(const char* path) const {
    std::ofstream file(path);
    file<<"P1\n"<<w_<<" "<<h_<<"\n";
    for(auto pixel:pixels)file<<(pixel?"0 ":"1 ");
  }
};
