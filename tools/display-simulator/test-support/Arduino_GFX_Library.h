#pragma once
#include <Arduino.h>
constexpr int GFX_NOT_DEFINED = -1;
class Arduino_ESP32SPI {
 public:
  Arduino_ESP32SPI(int dc, int cs, int clk, int mosi, int miso) {
    assert(dc == 4 && cs == 5 && clk == 6 && mosi == 7 && miso == -1);
  }
};
class Arduino_ST7789 {
 public:
  Arduino_ST7789(Arduino_ESP32SPI *, int rst, int rotation, bool ips,
                int w, int h, int x1, int y1, int x2, int y2) {
    assert(rst == 8 && rotation == 0 && ips && w == 240 && h == 280);
    assert(x1 == 0 && y1 == 20 && x2 == 0 && y2 == 0);
  }
  bool begin(int hz) { assert(hz == 40000000); ++begins; now_us += 600000; return begin_result; }
  void draw16bitRGBBitmap(int x, int y, uint16_t *colors, int w, int h) {
    assert(colors && x >= 0 && y >= 0 && x + w <= 240 && y + h <= 280);
    assert(w * h <= 240 * 20);
    for (int row = 0; row < h; ++row) for (int col = 0; col < w; ++col)
      panel_frame[(y + row) * 240 + x + col] = colors[row * w + col];
    ++bitmap_calls; bitmap_pixels += w * h; now_us += w * h / 2 + 1;
  }
  void fillScreen(uint16_t color) { panel_frame.fill(color); now_us += 30000; }
  void setTextWrap(bool) {}
  void setTextSize(int) {}
  void setTextColor(uint16_t, uint16_t) {}
  void setCursor(int, int) {}
  void print(const char *) { ++text_calls; now_us += 400; }
  void drawFastHLine(int, int, int, uint16_t) {}
  void drawRect(int, int, int, int, uint16_t) {}
  void fillRect(int x, int y, int w, int h, uint16_t color) {
    assert(x >= 0 && y >= 0 && x + w <= 240 && y + h <= 280);
    for (int row = y; row < y + h; ++row) for (int col = x; col < x + w; ++col)
      panel_frame[row * 240 + col] = color;
    now_us += 2000;
  }
};
