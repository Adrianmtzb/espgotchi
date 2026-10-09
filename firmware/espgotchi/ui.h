#pragma once
#include <Arduino.h>
#include "pet.h"

enum MenuItem : uint8_t { MENU_FEED = 0, MENU_SNACK, MENU_PLAY, MENU_PET, MENU_CLEAN, MENU_SLEEP, MENU_MEDS, MENU_INFO, MENU_COUNT };

struct NetInfo {
  bool connected;
  bool apMode;
  char ssid[33];
  char ip[16];
  int rssi;
  bool timeValid;
  char host[25];  // mDNS hostname without .local
};

struct Sprite;

class Ui {
 public:
  bool begin(uint8_t rotation);
  void render(const Pet &pet, const NetInfo &net, int8_t menuSel, bool infoPage, bool night);
  void setBacklight(uint8_t level);
  void bootAnimation(const Pet &pet);  // blocking ~1.7 s: the egg drops in, the name types out, the version shows
  void cornerTest();
  void crosshair(int16_t x, int16_t y, uint8_t idx, uint8_t total);  // 'tcal' CLI command: touch calibration target  // 'corners' CLI command: numbered marks along each corner diagonal to measure the bezel radius
  void dumpFramebuffer(Stream &out);  // 'shot' CLI command: base64 RGB565 for tools/screenshot.py
  // Menu cell under a screen point, or -1. Same geometry drawMenu() uses, so touch and pixels agree.
  int8_t menuHit(int16_t x, int16_t y) const;
  int16_t width() const { return W; }
  int16_t height() const { return H; }

 private:
  uint32_t frameMs = 0;
  uint8_t frame = 0;  // toggles every 500ms
  uint16_t bg, bg2, fg, muted, accent;
  int16_t W = 320, H = 172;
  bool portrait = false;
  // Layout derived from W/H in begin(): the room, and the side/bottom panel for stats or the menu.
  int16_t roomX = 8, roomY = 30, roomW = 200, roomH = 134, panelX = 218, panelY = 34, panelW = 94, statRowH = 26;
  int16_t roomR = 12, topInset = 0;  // room corner radius; horizontal inset of the top bar on rounded glass
  // Horizontal pixels hidden by a rounded corner at `dist` px from the top or bottom edge (0 on square glass).
  int16_t edgeInset(int16_t dist) const;
  void menuGrid(int16_t &cols, int16_t &x0, int16_t &y0) const;
  void pickTheme(const Pet &pet, bool night);
  void drawSprite(const Sprite &s, int16_t x, int16_t y, uint8_t scale, uint8_t maxCols = 255);
  void drawBubbles(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t t);
  void drawBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t pct);
  void drawTopBar(const Pet &pet, const NetInfo &net, bool nameOnly);
  void drawRoom(const Pet &pet, bool night);
  void drawStats(const Pet &pet);
  void drawMenu(int8_t sel);
  void drawInfo(const Pet &pet, const NetInfo &net);
  void text(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t size = 1);
  int16_t textWidth(const char *s, uint8_t size = 1);
};
