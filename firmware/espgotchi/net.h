#pragma once
#include <Arduino.h>
#include "config.h"
#include "pet.h"
#include "ui.h"

class Net {
 public:
  void begin(Pet *pet);
  void loop();
  NetInfo info() const;
  uint32_t epoch() const;  // 0 until NTP sync
  bool saveCredentials(const char *ssid, const char *pass);
  void forgetCredentials();
  void setTz(const char *tz);
  String tz() const;
  String savedSsid() const { return staSsid; }
  uint8_t brightness = BACKLIGHT_DAY;
  uint8_t rotation = 1;  // 1 landscape, 3 landscape flipped, 0 portrait, 2 portrait flipped
  bool restartRequested = false;
  void loadSettings();  // call before Ui::begin

 private:
  Pet *pet = nullptr;
  bool apMode = false;
  bool connected = false;
  bool mdnsStarted = false;
  bool timeConfigured = false;
  uint32_t connectStartMs = 0;
  uint32_t lastReconnectMs = 0;
  String staSsid, staPass, tzStr;
  void startAp();
  void startSta();
  void onConnected();
  void setupRoutes();
};
