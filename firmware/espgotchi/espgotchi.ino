// ESPgotchi: a virtual pet for small Waveshare ESP32 display boards.
// - Waveshare ESP32-C6-LCD-1.47: 172x320 ST7789, BOOT button, WS2812 mood LED
// - Waveshare ESP32-S3-Touch-LCD-1.69: 240x280 ST7789V2, touch, buzzer, battery, BOOT button
// - BOOT button: short press = cycle menu, long press = select, 6s hold = new egg
// - WiFi + HTTP JSON API (http://espgotchi.local) for the companion app
// The board is picked at compile time from the target (see config.h and boards/).
#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "hw.h"
#include "net.h"
#include "pet.h"
#include "touch.h"
#include "ui.h"

static Pet pet;
static Ui ui;
static Net net;
static Touch touch;

static int8_t menuSel = -1;  // -1 = menu hidden
static uint8_t lastSeenStage = STAGE_COUNT;
static PetAnim lastSeenAnim = ANIM_NONE;
static uint8_t lastSeenAnimFrame = 0;
static bool lastSeenLightsOff = false, lastSeenDead = false;
static uint32_t menuShownMs = 0;
static uint32_t infoUntilMs = 0;
static uint32_t lastTickMs = 0;
static uint32_t lastRenderMs = 0;
static uint32_t blOverrideUntil = 0;
static uint32_t cornerTestUntil = 0;
// Touch calibration: five targets in canvas coordinates; each tap logs canvas vs raw controller values.
static int8_t tcalIdx = -1;
static const uint8_t TCAL_N = 5;
static void tcalTarget(int16_t &x, int16_t &y) {
  const int16_t W = ui.width(), H = ui.height(), m = 40;
  const int16_t pts[TCAL_N][2] = {{(int16_t)(W / 2), (int16_t)(H / 2)}, {m, m}, {(int16_t)(W - m), m}, {m, (int16_t)(H - m)}, {(int16_t)(W - m), (int16_t)(H - m)}};
  x = pts[tcalIdx][0]; y = pts[tcalIdx][1];
}
static uint8_t blOverride = 0;

// ---------- button ----------
static bool btnDown = false;
static uint32_t btnDownMs = 0;
static bool btnLongFired = false;
static bool btnResetFired = false;

static void actionFeedback(bool ok, Tune okTune = TUNE_OK);  // Arduino's auto-prototype skips functions with default args
static void runMenuAction(int8_t item) {
  switch (item) {
    case MENU_FEED: actionFeedback(pet.feed(false)); break;
    case MENU_PLAY: actionFeedback(pet.play()); break;
    case MENU_PET: actionFeedback(pet.pet()); break;
    case MENU_CLEAN: actionFeedback(pet.clean()); break;
    case MENU_SLEEP: actionFeedback(pet.toggleLights()); break;  // tune comes from the lightsOff change in loop
    case MENU_MEDS: actionFeedback(pet.medicine()); break;
    case MENU_INFO: infoUntilMs = millis() + INFO_TIMEOUT_MS; hwTune(TUNE_INFO); break;
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
static void ledCelebrate(uint16_t ms) { ledRainbowUntil = millis() + ms; hwJingle(); }
// Accepted actions get their sound from the animation they start (see loop), so this only
// handles the refusal and the generic acknowledgement for actions without an animation.
static void actionFeedback(bool ok, Tune okTune) {
  if (ok) { ledFlash(255, 255, 255, 180); if (okTune != TUNE_OK) hwTune(okTune); }  // crisp white blink: done
  else { ledFlash(255, 0, 0, 350); hwTune(TUNE_NO); }                                // red / low grumble: refused
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
    hwTune(TUNE_TICK);
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
    hwTune(TUNE_TICK);
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
    hwTune(TUNE_INFO);
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

// ---------- touch (boards with a CST816) ----------
// Tap an icon = run it, tap elsewhere = same as a BOOT click, swipe = move through the menu,
// long press = same as holding BOOT. Everything funnels into the button handlers so the two
// inputs can never disagree about what the screen means.
static bool touchDebug = false;
static void pollTouch() {
  TouchEvent e = touch.poll();
  if (e.kind == TouchEvent::NONE) return;
  if (touchDebug) Serial.printf("[touch] kind=%d x=%d y=%d raw=%d,%d rot=%d menu=%d hit=%d\n", e.kind, e.x, e.y, e.rawX, e.rawY, net.rotation, menuSel, ui.menuHit(e.x, e.y));
  if (tcalIdx >= 0) {
    if (e.kind != TouchEvent::TAP) return;
    int16_t tx, ty;
    tcalTarget(tx, ty);
    Serial.printf("[tcal] %d target=%d,%d canvas=%d,%d raw=%d,%d rot=%d\n", tcalIdx, tx, ty, e.x, e.y, e.rawX, e.rawY, net.rotation);
    if (++tcalIdx >= (int8_t)TCAL_N) { tcalIdx = -1; Serial.println("[tcal] done"); }
    return;
  }
  ledFlash(90, 90, 120, 80);
  switch (e.kind) {
    case TouchEvent::TAP:
      if (menuSel >= 0 && !infoUntilMs) {
        int8_t hit = ui.menuHit(e.x, e.y);
        if (hit >= 0) { runMenuAction(hit); menuSel = -1; }
        else menuSel = -1;  // tapping the room closes the menu
      } else onShortPress();
      break;
    case TouchEvent::LONG_PRESS:
      onLongPress();
      break;
    case TouchEvent::SWIPE_LEFT:
    case TouchEvent::SWIPE_UP:
      if (menuSel >= 0) { menuSel = (menuSel + MENU_COUNT - 1) % MENU_COUNT; menuShownMs = millis(); hwTune(TUNE_TICK); }
      else if (!infoUntilMs && pet.state().stage != STAGE_EGG && !pet.state().dead) { menuSel = 0; menuShownMs = millis(); hwTune(TUNE_TICK); }
      break;
    case TouchEvent::SWIPE_RIGHT:
    case TouchEvent::SWIPE_DOWN:
      if (menuSel >= 0) { menuSel = (menuSel + 1) % MENU_COUNT; menuShownMs = millis(); hwTune(TUNE_TICK); }
      else if (!infoUntilMs && pet.state().stage != STAGE_EGG && !pet.state().dead) { menuSel = 0; menuShownMs = millis(); hwTune(TUNE_TICK); }
      break;
    default: break;
  }
}

// ---------- PWR key (boards with a power latch) ----------
// Holding PWR for two seconds cuts the battery rail. On USB the board stays on and the hold is
// just ignored, which matches how the stock Waveshare demo behaves.
static uint32_t pwrHeldSince = 0;
static void pollPowerKey() {
  if (!hwPowerKeyHeld()) { pwrHeldSince = 0; return; }
  if (!pwrHeldSince) pwrHeldSince = millis();
  else if (millis() - pwrHeldSince > 2000) {
    pet.save(net.epoch(), true);
    hwTune(TUNE_POWEROFF);
    for (uint8_t i = 0; i < 40; i++) { hwLoop(); delay(10); }  // let the tune finish
    hwPowerOff();
    pwrHeldSince = 0;
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
  hwLed((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
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
    Serial.println("commands: status | wifi <ssid> <pass> | forget | name <name> | tz <posix-tz> | host <name> | feed | snack | play | pet | clean | sleep | med | bl [0-255] | shot | press | hold | tp | tcal | gpio <n> | corners | boot | hatch | reset [kawaii|alien|dino|edge|ghost|pumpkin|mimi|momo|pingo] | reboot");
  } else if (cmd == "status") {
    JsonDocument doc;
    pet.toJson(doc.to<JsonObject>());
    serializeJsonPretty(doc, Serial);
    Serial.println();
    NetInfo n = net.info();
    Serial.printf("wifi: %s ssid=%s ip=%s rssi=%d ap=%d saved=\"%s\" time=%d tz=%s\n", n.connected ? "connected" : "down", n.ssid, n.ip, n.rssi, n.apMode, net.savedSsid().c_str(), n.timeValid, net.tz().c_str());
    Serial.printf("board: %s  host=%s.local  screen=%dx%d  touch=%d", BOARD_NAME, net.hostname().c_str(), ui.width(), ui.height(), touch.present());
    if (hwHasBattery()) Serial.printf("  battery=%dmV (%d%%)", hwBatteryMv(), hwBatteryPct());
    Serial.println();
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
  else if (cmd == "host") {
    if (net.setHostname(rest.c_str())) { Serial.printf("hostname %s.local saved, rebooting...\n", net.hostname().c_str()); delay(200); ESP.restart(); }
    else Serial.println("usage: host <1-24 chars of a-z 0-9 ->");
  }
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
  else if (cmd == "tp") { touchDebug = !touchDebug; Serial.printf("touch debug %s\n", touchDebug ? "on" : "off"); }
  else if (cmd == "gpio") Serial.printf("gpio %d = %d\n", (int)rest.toInt(), digitalRead(rest.toInt()));  // bring-up aid
  else if (cmd == "tcal") { tcalIdx = 0; Serial.println("touch calibration: tap each cross"); }
  else if (cmd == "boot") ui.bootAnimation(pet);
  else if (cmd == "corners") { cornerTestUntil = millis() + 60000; Serial.println("corner test on screen for 60 s"); }
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
  hwBegin();  // first: latches the battery rail on boards that have one
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  net.loadSettings();
  if (!ui.begin(net.rotation)) Serial.println("[ui] display init failed");
  if (HAS_TOUCH) touch.begin(net.rotation);
  pet.begin();
  ui.bootAnimation(pet);
  lastSeenLightsOff = pet.state().lightsOff;
  lastSeenDead = pet.state().dead;
  lastSeenAnim = pet.anim();
  net.begin(&pet);
  Serial.println("[sys] ready. type 'help' for commands");
}

void loop() {
  uint32_t now = millis();
  net.loop();
  hwLoop();
  pollButton();
  pollTouch();
  pollPowerKey();
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
  if (pet.anim() != lastSeenAnim) {  // one place for action feedback: button, touch, web and CLI
    PetAnim a = pet.anim();
    if (a != ANIM_NONE) ledFlash(255, 255, 255, 180);
    if (a == ANIM_NONE && (lastSeenAnim == ANIM_EAT || lastSeenAnim == ANIM_SNACK)) hwTune(TUNE_YUM);
    lastSeenAnimFrame = pet.animFrame();
    switch (a) {
      case ANIM_EAT: hwTune(TUNE_FEED); break;
      case ANIM_SNACK: hwTune(TUNE_SNACK); break;
      case ANIM_PLAY: hwTune(TUNE_PLAY); break;
      case ANIM_PET: hwTune(TUNE_PET); break;
      case ANIM_CLEAN: hwTune(TUNE_CLEAN); break;
      case ANIM_HEAL: hwTune(TUNE_MEDS); break;
      default: break;
    }
    lastSeenAnim = a;
  } else if ((lastSeenAnim == ANIM_EAT || lastSeenAnim == ANIM_SNACK) && pet.animFrame() != lastSeenAnimFrame) {
    lastSeenAnimFrame = pet.animFrame();  // every chewing frame gets its own bite
    hwTune(lastSeenAnim == ANIM_EAT ? TUNE_FEED : TUNE_SNACK);
  }
  if (pet.state().lightsOff != lastSeenLightsOff) {
    lastSeenLightsOff = pet.state().lightsOff;
    hwTune(lastSeenLightsOff ? TUNE_SLEEP : TUNE_WAKE);
  }
  if (pet.state().dead != lastSeenDead) {
    lastSeenDead = pet.state().dead;
    if (lastSeenDead) hwTune(TUNE_SAD);
  }
  openMenuIfClickExpired();
  // Signed compare: menuShownMs may be set later in this same iteration (button/CLI run after
  // `now` was sampled), and an unsigned subtraction would wrap and close the menu instantly.
  if (menuSel >= 0 && (int32_t)(millis() - menuShownMs) > (int32_t)MENU_TIMEOUT_MS) menuSel = -1;
  if (infoUntilMs && now > infoUntilMs) infoUntilMs = 0;

  if (now - lastRenderMs >= 100) {
    lastRenderMs = now;
    bool night = isNight() || pet.state().lightsOff;
    bool dim = night && (pet.state().lightsOff || net.nightDim);
    uint8_t bl = pet.state().lightsOff ? BACKLIGHT_NIGHT : (dim ? min<int>(net.brightness, BACKLIGHT_NIGHT * 2) : net.brightness);
    if (millis() < blOverrideUntil) bl = blOverride;
    ui.setBacklight(bl);
    if (millis() < cornerTestUntil) ui.cornerTest();
    else if (tcalIdx >= 0) { int16_t tx, ty; tcalTarget(tx, ty); ui.crosshair(tx, ty, tcalIdx, TCAL_N); }
    else ui.render(pet, net.info(), menuSel, infoUntilMs != 0, night);
    updateLed();
  }
  if (net.restartRequested) {
    delay(300);
    ESP.restart();
  }
}
