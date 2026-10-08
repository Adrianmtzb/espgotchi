#include "ui.h"
#include <Arduino_GFX_Library.h>
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "config.h"
#include "sprites.h"

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX *panel = nullptr;
static Arduino_Canvas *gfx = nullptr;

static const uint16_t C_GOOD = 0x4EE9, C_WARN = 0xFE60, C_BAD = 0xF9C7, C_WHITE = 0xFFFF, C_INK = 0x1082;
static inline uint16_t barColor(int16_t pct) { return pct > 50 ? C_GOOD : (pct > 25 ? C_WARN : C_BAD); }

// Blend two RGB565 colors (t = 0..255 towards b)
static uint16_t mix(uint16_t a, uint16_t b, uint8_t t) {
  uint16_t r = ((a >> 11) * (255 - t) + (b >> 11) * t) / 255;
  uint16_t g = (((a >> 5) & 0x3F) * (255 - t) + ((b >> 5) & 0x3F) * t) / 255;
  uint16_t bl = ((a & 0x1F) * (255 - t) + (b & 0x1F) * t) / 255;
  return (r << 11) | (g << 5) | bl;
}

bool Ui::begin(uint8_t rotation) {
  portrait = (rotation & 1) == 0;
  W = portrait ? LCD_NATIVE_W : LCD_NATIVE_H;
  H = portrait ? LCD_NATIVE_H : LCD_NATIVE_W;
  bus = new Arduino_ESP32SPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED, FSPI);
  panel = new Arduino_ST7789(bus, PIN_LCD_RST, rotation & 3, true, LCD_NATIVE_W, LCD_NATIVE_H, LCD_COL_OFFSET, 0, LCD_COL_OFFSET, 0);
  gfx = new Arduino_Canvas(W, H, panel, 0, 0, 0);
  if (!ledcAttach(PIN_LCD_BL, 5000, 8)) Serial.println("[ui] backlight PWM attach failed");
  setBacklight(0);
  if (!gfx->begin(80000000)) return false;
  gfx->fillScreen(C_INK);
  gfx->flush();
  setBacklight(BACKLIGHT_DAY);
  return true;
}

void Ui::setBacklight(uint8_t level) { ledcWrite(PIN_LCD_BL, level); }

void Ui::text(int16_t x, int16_t y, const char *s, uint16_t color, uint8_t size) {
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  gfx->setCursor(x, y);
  gfx->print(s);
}

int16_t Ui::textWidth(const char *s, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setTextSize(size);
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

void Ui::splash(const char *line1, const char *line2) {
  gfx->fillScreen(C_INK);
  gfx->setFont(&FreeSansBold12pt7b);
  text(16, 60, "espgotchi", C_WHITE);
  gfx->setFont(&FreeSans9pt7b);
  text(16, 95, line1, 0xBDF7);
  text(16, 118, line2, 0xBDF7);
  gfx->setFont(nullptr);
  drawSprite(SPR_KAWAII_EGG, W - 100, portrait ? 150 : 40, 3);
  gfx->flush();
}

// Dumps the canvas as base64 RGB565 so the real layout can be inspected off-device.
void Ui::dumpFramebuffer(Stream &out) {
  static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t *p = (const uint8_t *)gfx->getFramebuffer();
  size_t n = (size_t)W * H * 2;
  out.printf("SHOT %d %d\n", W, H);
  char line[77];
  uint8_t col = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? p[i + 1] : 0) << 8 | (i + 2 < n ? p[i + 2] : 0);
    line[col++] = B64[(v >> 18) & 63]; line[col++] = B64[(v >> 12) & 63];
    line[col++] = i + 1 < n ? B64[(v >> 6) & 63] : '='; line[col++] = i + 2 < n ? B64[v & 63] : '=';
    if (col >= 76) { line[col] = 0; out.println(line); col = 0; }
  }
  if (col) { line[col] = 0; out.println(line); }
  out.println("END");
}

void Ui::drawSprite(const Sprite &s, int16_t x, int16_t y, uint8_t scale, uint8_t maxCols) {
  for (uint8_t r = 0; r < s.h; r++) {
    for (uint8_t c = 0; c < s.w && c < maxCols; c++) {
      uint8_t idx = s.px[r * s.w + c];
      if (idx == SPR_TRANSPARENT) continue;
      gfx->fillRect(x + c * scale, y + r * scale, scale, scale, s.palette[idx]);
    }
  }
}

// Soap bubbles rising inside (x,y,w,h); t drives the motion (ms).
void Ui::drawBubbles(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t t) {
  const uint16_t soap = gfx->color565(190, 235, 255), shine = C_WHITE;
  for (uint8_t i = 0; i < 9; i++) {
    uint32_t ph = (t + i * 311) % 1800;                 // each bubble on its own cycle
    int16_t bx = x + ((i * 37 + 11) % (w - 16)) + 8 + ((ph / 150) % 3) - 1;  // slight wobble
    int16_t by = y + h - 6 - (int32_t)ph * (h - 10) / 1800;
    uint8_t rad = 2 + (i % 3);
    gfx->drawCircle(bx, by, rad, soap);
    if (rad > 2) gfx->drawPixel(bx - 1, by - 1, shine);
  }
}

void Ui::drawBar(int16_t x, int16_t y, int16_t w, int16_t h, int16_t pct) {
  gfx->fillRoundRect(x, y, w, h, h / 2, mix(bg, fg, 40));
  int16_t fill = w * pct / 100;
  if (fill >= h) gfx->fillRoundRect(x, y, fill, h, h / 2, barColor(pct));
  else if (fill > 0) gfx->fillCircle(x + h / 2, y + h / 2, h / 2 - 1, barColor(pct));
}

void Ui::pickTheme(const Pet &pet, bool night) {
  const SpeciesInfo &sp = SPECIES[pet.state().species < SPECIES_COUNT ? pet.state().species : 0];
  accent = sp.accent;
  if (night) { bg = sp.nightBg; bg2 = sp.nightBg2; fg = 0xE73C; }
  else { bg = sp.dayBg; bg2 = sp.dayBg2; fg = 0x2124; }
  muted = mix(fg, bg, 110);
}

void Ui::drawTopBar(const Pet &pet, const NetInfo &net) {
  const PetState &s = pet.state();
  gfx->setFont(portrait ? &FreeSans9pt7b : &FreeSansBold12pt7b);
  text(8, portrait ? 18 : 21, s.name, fg);
  gfx->setFont(nullptr);
  // stage / age pill
  char buf[20];
  if (s.stage == STAGE_EGG) snprintf(buf, sizeof(buf), "egg  %lus", (unsigned long)s.eggSec);
  else if (s.ageSec < 3600) snprintf(buf, sizeof(buf), "%s  %lum", pet.stageName(), (unsigned long)(s.ageSec / 60));
  else snprintf(buf, sizeof(buf), "%s  %luh", pet.stageName(), (unsigned long)(s.ageSec / 3600));
  int16_t w = textWidth(buf) + 14;
  int16_t x = W - 22 - w;
  gfx->fillRoundRect(x, 6, w, 16, 8, accent);
  text(x + 7, 10, buf, C_INK);
  // wifi dot
  uint16_t wc = net.connected ? C_GOOD : (net.apMode ? C_WARN : C_BAD);
  gfx->fillCircle(W - 11, 14, 4, wc);
  if (!net.connected && frame) gfx->drawCircle(W - 11, 14, 6, wc);
}

void Ui::drawRoom(const Pet &pet, bool night) {
  const PetState &s = pet.state();
  const int16_t rx = portrait ? 6 : 8, ry = 30, rw = portrait ? W - 12 : 200, rh = portrait ? 150 : 134, floorY = ry + rh - 22;
  gfx->fillRoundRect(rx, ry, rw, rh, 12, bg2);
  gfx->fillRoundRect(rx, floorY, rw, rh - (floorY - ry), 12, mix(bg2, fg, 30));
  gfx->fillRect(rx, floorY, rw, 12, mix(bg2, fg, 30));
  if (night) {
    static const uint8_t stars[][2] = {{20, 14}, {60, 8}, {110, 20}, {150, 10}, {185, 26}, {90, 40}, {170, 48}};
    for (auto &st : stars) gfx->drawPixel(rx + st[0], ry + st[1], (frame ^ (st[0] & 1)) ? C_WHITE : muted);
  }

  const SpeciesInfo &sp = SPECIES[s.species < SPECIES_COUNT ? s.species : 0];
  const Sprite *spr = sp.egg;
  const uint8_t scale = 4;
  int16_t px = rx + (rw - 24 * scale) / 2, py = floorY + 4 - 24 * scale;
  int16_t dx = 0, dy = 0;
  PetAnim anim = pet.anim();

  if (s.dead) {
    spr = &SPR_SKULL;
    px = rx + (rw - 16 * 5) / 2;
    py = floorY - 16 * 5 - 6;
    drawSprite(*spr, px, py, 5);
    gfx->setFont(&FreeSans9pt7b);
    text(rx + 60, ry + 22, "R.I.P.", fg);
    gfx->setFont(nullptr);
    text(rx + (portrait ? 4 : 14), floorY + 7, portrait ? "hold BOOT 6s: new egg" : "hold BOOT 6s for a new egg", muted);
    return;
  }
  if (s.stage == STAGE_EGG) {
    dx = frame ? 2 : -2;
    if (s.eggSec > 45) dy = frame ? -3 : 0;
    drawSprite(*spr, px + dx, py + dy, scale);
    text(rx + (portrait ? 8 : 22), floorY + 7, portrait ? "BOOT: hatch early" : "press BOOT to hatch early", muted);
    return;
  }
  bool alt = frame && !s.asleep;
  spr = (s.asleep && anim == ANIM_NONE) ? sp.sleep[pet.spriteSlot()] : sp.frames[pet.spriteSlot()][alt ? 1 : 0];
  if (!s.asleep && anim == ANIM_NONE) dx = frame ? 2 : -2;
  if (anim == ANIM_PLAY) dy = (pet.animFrame() % 2) ? -16 : 0;
  if (anim == ANIM_HATCH) dy = frame ? -6 : 0;
  // soft shadow
  gfx->fillRoundRect(px + 20, floorY - 1, 56, 6, 3, mix(bg2, fg, 60));
  drawSprite(*spr, px + dx, py + dy, scale);

  // overlays
  const int16_t ex = px + 24 * scale - 14, ey = py - 4;
  (void)ex;
  if (anim == ANIM_EAT || anim == ANIM_SNACK) {
    uint8_t f = pet.animFrame();  // 0 whole, 1 bitten, 2+ crumbs
    const Sprite *food = anim == ANIM_SNACK ? SNACKS[pet.animItem() % SNACKS_COUNT] : MEALS[pet.animItem() % MEALS_COUNT];
    if (f >= 2) drawSprite(SPR_CRUMBS, rx + 12, floorY - 44, 3);
    else drawSprite(*food, rx + 12, floorY - 44, 3, f == 0 ? 255 : 9);
  }
  if (anim == ANIM_PET || anim == ANIM_HEAL) drawSprite(SPR_HEART, ex, ey + (frame ? 0 : 4), 2);
  if (anim == ANIM_PLAY) {
    // ball bouncing next to the pet (triangle wave, 600 ms period)
    uint16_t t = millis() % 600;
    int16_t bounce = (t < 300 ? t : 600 - t) * 40 / 300;
    drawSprite(SPR_BALL, rx + 14, floorY - 32 - bounce, 2);
  }
  if (anim == ANIM_CLEAN) {
    drawSprite(SPR_ICON_CLEAN, ex, ey, 2);
    drawBubbles(px - 12, py - 10, 24 * scale + 24, 24 * scale + 10, millis());
  }
  if (s.asleep && anim == ANIM_NONE) drawSprite(SPR_ZZZ, ex, ey + (frame ? 0 : 3), 2);
  if (s.sick) drawSprite(SPR_SICK, rx + 14, py + 10, 2);
  if (!s.asleep && anim == ANIM_NONE && pet.needsAttention() && frame) drawSprite(SPR_ALERT, ex, ey, 2);
  for (uint8_t i = 0; i < s.poops; i++) drawSprite(SPR_POOP, rx + rw - 44 - i * 30, floorY - 28, 2);
}

void Ui::drawStats(const Pet &pet) {
  const PetState &s = pet.state();
  struct { const char *label; int16_t v; } rows[5] = {
    {"FOOD", s.hunger}, {"FUN", s.happiness}, {"ENERGY", s.energy}, {"CLEAN", s.hygiene}, {"HEALTH", s.health}};
  const int16_t x = portrait ? 10 : 218, w = portrait ? W - 20 : 94;
  int16_t y = portrait ? 192 : 34;
  for (auto &r : rows) {
    text(x, y, r.label, muted);
    char v[6];
    snprintf(v, sizeof(v), "%d", r.v);
    text(x + w - textWidth(v), y, v, fg);
    drawBar(x, y + 10, w, 7, r.v);
    y += portrait ? 25 : 26;
  }
}

void Ui::drawMenu(int8_t sel) {
  const Sprite *icons[MENU_COUNT] = {&SPR_MEAL_BURGER, &SPR_BALL, &SPR_HEART, &SPR_ICON_CLEAN, &SPR_ICON_SLEEP, &SPR_ICON_MEDS, &SPR_ICON_INFO};
  const char *labels[MENU_COUNT] = {"Feed", "Play", "Pet", "Clean", "Lights", "Medicine", "Info"};
  // 7 items: 2x4 grid over the stats panel (landscape) or 4x2 under the room (portrait)
  const int16_t cell = 40, gap = 2, cols = portrait ? 4 : 2;
  const int16_t x0 = portrait ? (W - (cols * cell + (cols - 1) * gap)) / 2 : 218, y0 = portrait ? 192 : 4;
  for (int i = 0; i < MENU_COUNT; i++) {
    int16_t cx = x0 + (i % cols) * (cell + gap), cy = y0 + (i / cols) * (cell + gap);
    gfx->fillRoundRect(cx, cy, cell, cell, 10, i == sel ? accent : mix(bg, fg, 25));
    drawSprite(*icons[i], cx + 6, cy + 6, 2);
  }
  int16_t rowsUsed = (MENU_COUNT + cols - 1) / cols;
  // Landscape has no room under the grid: label + hint go on the room floor strip instead
  const int16_t lx = portrait ? x0 : 18, ly = portrait ? y0 + rowsUsed * (cell + gap) + 12 : 30 + 134 - 22 + 16;
  gfx->setFont(&FreeSans9pt7b);
  text(lx, ly, labels[sel], fg);
  gfx->setFont(nullptr);
  const char *hint = "hold BOOT to select";
  if (portrait) text(x0, H - 10, hint, muted);
  else text(8 + 200 - 8 - textWidth(hint), 30 + 134 - 22 + 8, hint, muted);
}

void Ui::drawInfo(const Pet &pet, const NetInfo &net) {
  const PetState &s = pet.state();
  gfx->fillScreen(C_INK);
  gfx->setFont(&FreeSansBold12pt7b);
  text(10, 22, s.name, C_WHITE);
  gfx->setFont(nullptr);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s  gen %u", SPECIES[s.species < SPECIES_COUNT ? s.species : 0].label, s.generation);
  if (portrait) text(10, 30, buf, 0xBDF7);
  else text(W - 12 - textWidth(buf), 10, buf, 0xBDF7);
  int16_t y = portrait ? 48 : 38;
  auto line = [&](uint16_t color, const char *fmt, auto... args) {
    gfx->setCursor(10, y);
    gfx->setTextColor(color);
    gfx->printf(fmt, args...);
    y += 13;
  };
  if (portrait) {
    line(C_WHITE, "%s %s", pet.formName(), pet.stageName());
    line(C_WHITE, "Age %luh %02lum  Weight %d", (unsigned long)(s.ageSec / 3600), (unsigned long)((s.ageSec / 60) % 60), s.weight);
    line(C_WHITE, "Mood %s%s", pet.moodWord(), s.sick ? " (sick)" : "");
    line(C_WHITE, "Care mistakes %u", s.careMistakes);
  } else {
    line(C_WHITE, "Stage %s %s   Age %luh %02lum   Weight %d", pet.formName(), pet.stageName(), (unsigned long)(s.ageSec / 3600), (unsigned long)((s.ageSec / 60) % 60), s.weight);
    line(C_WHITE, "Mood %s%s   Care mistakes %u", pet.moodWord(), s.sick ? " (sick)" : "", s.careMistakes);
  }
  uint32_t nxt = pet.nextEvolutionSec();
  if (nxt) line(0xBDF7, "Next evolution in %luh %02lum", (unsigned long)(nxt / 3600), (unsigned long)((nxt / 60) % 60));
  y += 8;
  if (net.connected) {
    line(C_GOOD, "WiFi %s  (%d dBm)", net.ssid, net.rssi);
    line(C_WHITE, "http://%s/", net.ip);
    line(C_WHITE, "http://%s.local/", MDNS_HOST);
  } else if (net.apMode) {
    line(C_WARN, "Setup network: %s", net.ssid);
    line(C_WHITE, "Open network, no password");
    line(C_WHITE, portrait ? "Join it, setup page opens" : "Join it and the setup page pops up");
  } else {
    line(C_WARN, "WiFi connecting...");
  }
  y += 8;
  line(0xBDF7, "Time %s  FW %s  heap %luk", net.timeValid ? "ok" : "no", FW_VERSION, (unsigned long)(ESP.getFreeHeap() / 1024));
  text(10, H - 10, "press BOOT to go back", 0x7BEF);
}

void Ui::render(const Pet &pet, const NetInfo &net, int8_t menuSel, bool infoPage, bool night) {
  uint32_t now = millis();
  if (now - frameMs >= 500) {
    frameMs = now;
    frame ^= 1;
  }
  if (infoPage) {
    drawInfo(pet, net);
    gfx->flush();
    return;
  }
  pickTheme(pet, night);
  gfx->fillScreen(bg);
  drawTopBar(pet, net);
  drawRoom(pet, night);
  if (menuSel >= 0) drawMenu(menuSel);
  else drawStats(pet);
  gfx->flush();
}
