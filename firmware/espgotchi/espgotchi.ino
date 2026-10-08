// ESPgotchi for the Waveshare ESP32-C6-LCD-1.47
// - 1.47" ST7789 LCD (172x320) used in landscape
// - single BOOT button: short press = cycle menu, long press = select, 6s hold = new egg
// - WS2812 LED shows the pet's mood
// - WiFi + HTTP JSON API (http://espgotchi.local) for the companion app
#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "net.h"
#include "pet.h"
#include "ui.h"

static Pet pet;
static Ui ui;
static Net net;

static int8_t menuSel = -1;  // -1 = menu hidden
static uint8_t lastSeenStage = STAGE_COUNT;
static PetAnim lastSeenAnim = ANIM_NONE;
static uint32_t menuShownMs = 0;
static uint32_t infoUntilMs = 0;
static uint32_t lastTickMs = 0;
static uint32_t lastRenderMs = 0;
static uint32_t blOverrideUntil = 0;
static uint8_t blOverride = 0;

// ---------- button ----------
static bool btnDown = false;
static uint32_t btnDownMs = 0;
static bool btnLongFired = false;
static bool btnResetFired = false;

static void runMenuAction(int8_t item) {
  switch (item) {
    case MENU_FEED: actionFeedback(pet.feed(false)); break;
    case MENU_PLAY: actionFeedback(pet.play()); break;
    case MENU_PET: actionFeedback(pet.pet()); break;
    case MENU_CLEAN: actionFeedback(pet.clean()); break;
    case MENU_SLEEP: actionFeedback(pet.toggleLights()); break;
    case MENU_MEDS: actionFeedback(pet.medicine()); break;
    case MENU_INFO: infoUntilMs = millis() + INFO_TIMEOUT_MS; break;
  }
  pet.save(net.epoch(), true);
}

// ---------- LED feedback ----------
// A short colored flash on top of the mood color, and a rainbow sweep on hatch/evolution.
static uint32_t ledFlashUntil = 0, ledRainbowUntil = 0;
static uint8_t ledFlashR = 0, ledFlashG = 0, ledFlashB = 0;
static void ledFlash(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  ledFlashR = r; ledFlashG = g; ledFlashB = b;
  ledFlashUntil = millis() + ms;
}
static void ledCelebrate(uint16_t ms) { ledRainbowUntil = millis() + ms; }
static void actionFeedback(bool ok) {
  if (ok) ledFlash(255, 255, 255, 180);  // crisp white blink: done
  else ledFlash(255, 0, 0, 350);         // red: refused (asleep, full, dead...)
}

// Double-click on BOOT (menu closed) = pet the creature; a single click waits
// BUTTON_DOUBLE_CLICK_MS to tell them apart.
static uint32_t pendingClickMs = 0;

static void onShortPress() {
  if (infoUntilMs) { infoUntilMs = 0; return; }
  if (pet.state().dead) return;
  if (pet.state().stage == STAGE_EGG) { actionFeedback(pet.hatch()); return; }
  if (menuSel >= 0) {
    menuSel = (menuSel + 1) % MENU_COUNT;
    menuShownMs = millis();
    return;
  }
  if (pendingClickMs) {  // second click: pet
    pendingClickMs = 0;
    actionFeedback(pet.pet());
    return;
  }
  pendingClickMs = millis();
}

static void openMenuIfClickExpired() {
  if (pendingClickMs && millis() - pendingClickMs > BUTTON_DOUBLE_CLICK_MS) {
    pendingClickMs = 0;
    menuSel = 0;
    menuShownMs = millis();
  }
}

static void onLongPress() {
  if (infoUntilMs) { infoUntilMs = 0; return; }
  if (pet.state().dead) return;
  if (menuSel >= 0) {
    runMenuAction(menuSel);
    menuSel = -1;
  } else {
    infoUntilMs = millis() + INFO_TIMEOUT_MS;
  }
}

static void pollButton() {
  bool pressed = digitalRead(PIN_BOOT_BTN) == LOW;
  uint32_t now = millis();
  if (pressed && !btnDown) {
    ledFlash(90, 90, 120, 80);  // touch acknowledged
    btnDown = true;
    btnDownMs = now;
    btnLongFired = false;
    btnResetFired = false;
  } else if (pressed && btnDown) {
    uint32_t held = now - btnDownMs;
    if (!btnResetFired && held >= BUTTON_RESET_PRESS_MS) {
      btnResetFired = true;
      ledCelebrate(2000);
      pet.reset();
      pet.save(net.epoch(), true);
      menuSel = -1;
      infoUntilMs = 0;
    } else if (!btnLongFired && held >= BUTTON_LONG_PRESS_MS) {
      btnLongFired = true;
      onLongPress();
    }
  } else if (!pressed && btnDown) {
    btnDown = false;
    if (!btnLongFired && !btnResetFired && now - btnDownMs > 30) onShortPress();
  }
}

// ---------- mood LED ----------
static void updateLed() {
  const PetState &s = pet.state();
  uint8_t r = 0, g = 0, b = 0;
  uint32_t now = millis();
  float breathe = 0.55f + 0.45f * sinf(now / 900.0f);
  if (now < ledRainbowUntil) {  // hatch / evolution: hue sweep
    float h = fmodf(now / 400.0f, 6.0f), f = h - (int)h;
    uint8_t x = (uint8_t)(255 * (1 - f)), y = (uint8_t)(255 * f);
    switch ((int)h) { case 0: r = 255; g = y; break; case 1: r = x; g = 255; break; case 2: g = 255; b = y; break;
                      case 3: g = x; b = 255; break; case 4: r = y; b = 255; break; default: r = 255; b = x; }
    breathe = 1.0f;
  } else if (now < ledFlashUntil) {
    r = ledFlashR; g = ledFlashG; b = ledFlashB; breathe = 1.0f;
  } else if (s.dead) { r = 60; b = 60; breathe = 0.5f; }
  else if (s.stage == STAGE_EGG) { r = g = b = 255; }
  else if (s.asleep) { b = 255; breathe *= 0.35f; }
  else if (s.sick || s.health < 30) { r = 255; breathe = (now % 500) < 250 ? 1.0f : 0.15f; }  // urgent: hard blink
  else if (pet.needsAttention()) { r = 255; g = 120; breathe = ((now % 2000) < 120 || ((now % 2000) > 240 && (now % 2000) < 360)) ? 1.0f : 0.08f; }  // double blink every 2 s
  else { g = 255; r = 40; }
  float k = breathe * LED_MAX_BRIGHTNESS / 255.0f;
  rgbLedWrite(PIN_RGB_LED, (uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
}

// ---------- serial CLI ----------
static void handleSerialLine(String line) {
  line.trim();
  if (!line.length()) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String rest = sp < 0 ? "" : line.substring(sp + 1);
  rest.trim();
  cmd.toLowerCase();
  if (cmd == "help") {
    Serial.println("commands: status | wifi <ssid> <pass> | forget | name <name> | tz <posix-tz> | feed | snack | play | pet | clean | sleep | med | bl [0-255] | shot | press | hold | hatch | reset [kawaii|alien|dino|edge|ghost|pumpkin] | reboot");
  } else if (cmd == "status") {
    JsonDocument doc;
    pet.toJson(doc.to<JsonObject>());
    serializeJsonPretty(doc, Serial);
    Serial.println();
    NetInfo n = net.info();
    Serial.printf("wifi: %s ssid=%s ip=%s rssi=%d ap=%d saved=\"%s\" time=%d tz=%s\n", n.connected ? "connected" : "down", n.ssid, n.ip, n.rssi, n.apMode, net.savedSsid().c_str(), n.timeValid, net.tz().c_str());
  } else if (cmd == "forget") {
    net.forgetCredentials();
    Serial.println("wifi credentials erased, rebooting into setup mode...");
    delay(200);
    ESP.restart();
  } else if (cmd == "wifi") {
    int sp2 = rest.indexOf(' ');
    String ssid = sp2 < 0 ? rest : rest.substring(0, sp2);
    String pass = sp2 < 0 ? "" : rest.substring(sp2 + 1);
    if (net.saveCredentials(ssid.c_str(), pass.c_str())) {
      Serial.println("saved, rebooting...");
      delay(200);
      ESP.restart();
    } else Serial.println("usage: wifi <ssid> <pass>");
  } else if (cmd == "name") { pet.setName(rest.c_str()); pet.save(net.epoch(), true); }
  else if (cmd == "tz") { net.setTz(rest.c_str()); Serial.println("tz set"); }
  else if (cmd == "feed") pet.feed(false);
  else if (cmd == "snack") pet.feed(true);
  else if (cmd == "play") pet.play();
  else if (cmd == "pet") pet.pet();
  else if (cmd == "clean") pet.clean();
  else if (cmd == "sleep") pet.toggleLights();
  else if (cmd == "med") pet.medicine();
  else if (cmd == "hatch") pet.hatch();
  else if (cmd == "reset") { pet.reset(Pet::speciesFromKey(rest.length() ? rest.c_str() : nullptr)); pet.save(net.epoch(), true); }
  else if (cmd == "shot") ui.dumpFramebuffer(Serial);
  else if (cmd == "press") onShortPress();   // simulate BOOT gestures from the CLI
  else if (cmd == "hold") onLongPress();
  else if (cmd == "bl") {
    // Backlight diagnostics: 'bl' prints the LEDC duty, 'bl <0-255>' forces a value for 5 s
    Serial.printf("backlight duty=%lu freq=%lu setting=%u\n", (unsigned long)ledcRead(PIN_LCD_BL), (unsigned long)ledcReadFreq(PIN_LCD_BL), net.brightness);
    if (rest.length()) { blOverrideUntil = millis() + 5000; blOverride = constrain(rest.toInt(), 0, 255); }
  }
  else if (cmd == "reboot") ESP.restart();
  else Serial.println("unknown command, try 'help'");
}

static void pollSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleSerialLine(buf); buf = ""; }
    else if (buf.length() < 120) buf += c;
  }
}

static bool isNight() {
  if (!net.epoch()) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_hour >= 22 || t.tm_hour < 7;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  rgbLedWrite(PIN_RGB_LED, 0, 0, 0);
  net.loadSettings();
  if (!ui.begin(net.rotation)) Serial.println("[ui] display init failed");
  ui.splash("booting...", "");
  pet.begin();
  net.begin(&pet);
  Serial.println("[sys] ready. type 'help' for commands");
}

void loop() {
  uint32_t now = millis();
  net.loop();
  pollButton();
  pollSerial();

  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    uint8_t before = pet.state().stage;
    pet.tick(net.epoch());
    if (pet.state().stage != before) ledCelebrate(4000);
  }
  if (pet.state().stage != lastSeenStage) {  // also covers hatch/reset from button, web or CLI
    if (lastSeenStage != STAGE_COUNT && pet.state().stage != STAGE_EGG) ledCelebrate(4000);
    lastSeenStage = pet.state().stage;
  }
  if (pet.anim() != lastSeenAnim) {  // actions coming from the web/CLI light the LED too
    if (pet.anim() != ANIM_NONE) ledFlash(255, 255, 255, 180);
    lastSeenAnim = pet.anim();
  }
  openMenuIfClickExpired();
  // Signed compare: menuShownMs may be set later in this same iteration (button/CLI run after
  // `now` was sampled), and an unsigned subtraction would wrap and close the menu instantly.
  if (menuSel >= 0 && (int32_t)(millis() - menuShownMs) > (int32_t)MENU_TIMEOUT_MS) menuSel = -1;
  if (infoUntilMs && now > infoUntilMs) infoUntilMs = 0;

  if (now - lastRenderMs >= 100) {
    lastRenderMs = now;
    bool night = isNight() || pet.state().lightsOff;
    uint8_t bl = pet.state().lightsOff ? BACKLIGHT_NIGHT : (night ? min<int>(net.brightness, BACKLIGHT_NIGHT * 2) : net.brightness);
    if (millis() < blOverrideUntil) bl = blOverride;
    ui.setBacklight(bl);
    ui.render(pet, net.info(), menuSel, infoUntilMs != 0, night);
    updateLed();
  }
  if (net.restartRequested) {
    delay(300);
    ESP.restart();
  }
}
