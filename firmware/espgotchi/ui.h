#pragma once
#include <Arduino.h>
#include "pet.h"

enum MenuItem : uint8_t { MENU_FEED = 0, MENU_PLAY, MENU_PET, MENU_CLEAN, MENU_SLEEP, MENU_MEDS, MENU_INFO, MENU_COUNT };

struct NetInfo {
  bool connected;
  bool apMode;
  char ssid[33];
  char ip[16];
  int rssi;
  bool timeValid;
};

struct Sprite;

class Ui {
 public:
  bool begin(uint8_t rotation);
  void render(const Pet &pet, const NetInfo &net, int8_t menuSel, bool infoPage, bool night);
  void setBacklight(uint8_t level);
  void splash(const char *line1, const char *line2);
  void dumpFramebuffer(Stream &out);  // 'shot' CLI command: base64 RGB565 for tools/screenshot.py

 private:
  uint32_t frameMs = 0;
  uint8_t frame = 0;  // toggles every 500ms
  uint16_t bg, bg2, fg, muted, accent;
  int16_t W = 320, H = 172;
  bool portrait = false;
  void pickTheme(const Pet &pet, bool night);
  void drawSprite(const Sprite &s, int16_t x, int16_t y, uint8_t scale, uint8_t maxCols = 255);
  void drawBubbles(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t t);
  void drawBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t pct);
  void drawTopBar(const Pet &pet, const NetInfo &net);
  void drawRoom(const Pet &pet, bool night);
  void drawStats(const Pet &pet);
  void drawMenu(int8_t sel);
  void drawInfo(const Pet &pet, const NetInfo &net);
  void text(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t size = 1);
  int16_t textWidth(const char *s, uint8_t size = 1);
};
