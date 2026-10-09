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
  uint32_t epoch() const;  // 0 until the clock is set (NTP, or a browser on the setup network)
  // Set the clock from outside (the web panel sends its own time while there is no internet).
  // Rejected while the home network (and NTP) is up, and for epochs that are obviously wrong.
  bool setClock(uint32_t epoch);
  // Open the setup network now without touching the saved credentials, e.g. to take the pet
  // somewhere with no WiFi. STA stays off until the next boot.
  void openAp();
  bool saveCredentials(const char *ssid, const char *pass);
  void forgetCredentials();
  void setTz(const char *tz);
  String tz() const;
  // mDNS hostname (no ".local"): 1-24 chars of [a-z0-9-]; applied on the next boot. False if invalid.
  bool setHostname(const char *name);
  String hostname() const { return host; }
  String savedSsid() const { return staSsid; }
  uint8_t brightness = BACKLIGHT_DAY;
  uint8_t rotation = 1;  // 1 landscape, 3 landscape flipped, 0 portrait, 2 portrait flipped
  bool nightDim = true;  // cap the backlight between 22:00 and 07:00
  bool restartRequested = false;
  void loadSettings();  // call before Ui::begin
  // Visits: look for another ESPgotchi on the LAN now (async, the outcome is logged), or fetch a
  // given host (IP, "name.local" or a bare mDNS name) right away. visitHost blocks the loop for a
  // few hundred ms at most; on failure `err` says why.
  void visitNow();
  bool visitHost(const char *host, String &err);

 private:
  Pet *pet = nullptr;
  bool apMode = false;
  bool connected = false;
  bool mdnsStarted = false;
  bool timeConfigured = false;
  bool apHeld = false;  // openAp(): keep the AP, do not retry STA
  String host;
  uint32_t connectStartMs = 0;
  uint32_t lastReconnectMs = 0;
  uint32_t nextVisitMs = 0, lastAdvertMs = 0;
  String lastAdvert;
  void *visitSearch = nullptr;  // mdns_search_once_t* while a discovery is in flight
  String staSsid, staPass, tzStr;
  void advertise();
  void visitLoop();
  bool visitAddr(IPAddress ip, uint16_t port, String &err);
  void startAp();
  void startSta();
  void onConnected();
  void startMdns();
  void closeAp();
  void setupRoutes();
};
