#include "ui.h"
#include <Arduino_GFX_Library.h>
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "config.h"
#include "hw.h"
#include "sprites.h"

static Arduino_DataBus *bus = nullptr;
static Arduino_GFX *panel = nullptr;
static Arduino_Canvas *gfx = nullptr;

static const int16_t MENU_CELL = 40, MENU_GAP = 2;
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
  panel = new Arduino_ST7789(bus, PIN_LCD_RST, rotation & 3, true, LCD_NATIVE_W, LCD_NATIVE_H,
                             LCD_COL_OFFSET, LCD_ROW_OFFSET, LCD_COL_OFFSET2, LCD_ROW_OFFSET2);
  gfx = new Arduino_Canvas(W, H, panel, 0, 0, 0);
  // Layout. Tuned on the 320x172 panel (room 200x134, panel 94 wide) and expressed relative to
  // W/H so a 280x240 or 240x280 screen gets the same proportions.
  // Rounded glass (LCD_CORNER_RADIUS > 0): the top bar moves inward, the room's corner radius
  // follows the bezel so the room looks concentric with the glass, and the lowest stat row in
  // portrait shrinks to stay visible.
  topInset = edgeInset(6);
  if (portrait) {
    roomX = 6; roomY = 30; roomW = W - 12;
    roomH = min<int16_t>(150, H - 160);  // leaves room for five stat rows or the 4x2 menu
    panelY = roomY + roomH + 12;
    statRowH = (H - panelY - 3) / 5;
    int16_t pad = edgeInset(H - (panelY + 4 * statRowH + 17));
    panelX = 10 + pad; panelW = W - 20 - 2 * pad;
  } else {
    roomX = 8; roomY = 30; roomW = W - 120; roomH = H - 38;
    panelX = roomX + roomW + 10; panelW = W - panelX - 8; panelY = 34;
    statRowH = 26;
  }
  roomR = LCD_CORNER_RADIUS ? LCD_CORNER_RADIUS - roomX : 12;
  // Channel 0 on its own timer; the buzzer (hw.cpp) uses channel 2 so its tones never retune this one.
  if (!ledcAttachChannel(PIN_LCD_BL, 5000, 8, 0)) Serial.println("[ui] backlight PWM attach failed");
  setBacklight(0);
  if (!gfx->begin(80000000)) return false;
  gfx->fillScreen(C_INK);
  gfx->flush();
  setBacklight(BACKLIGHT_DAY);
  return true;
}

void Ui::setBacklight(uint8_t level) { ledcWrite(PIN_LCD_BL, level); }

int16_t Ui::edgeInset(int16_t dist) const {
  const int32_t r = LCD_CORNER_RADIUS;
  if (!r || dist >= r) return 0;
  if (dist < 0) dist = 0;
  int32_t dy = r - dist;
  return (int16_t)(r - (int32_t)sqrtf((float)(r * r - dy * dy)));
}

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

// Boot sequence, drawn frame by frame with millis() so it looks the same on both boards: the
// species egg drops from above and bounces onto its shadow, the name types itself out, then the
// version and board appear. The buzzer sequencer keeps running so the power-on tune plays through.
void Ui::bootAnimation(const Pet &pet) {
  const PetState &st = pet.state();
  const SpeciesInfo &sp = SPECIES[st.species < SPECIES_COUNT ? st.species : 0];
  const uint8_t scale = 3;
  const int16_t eggSize = 24 * scale;
  const int16_t blockH = eggSize + 58;  // egg, title, version
  const int16_t top = max<int16_t>(12, (H - blockH) / 2);
  const int16_t eggX = W / 2 - eggSize / 2;
  const int16_t floorY = top + eggSize;  // egg rests on this line, shadow sits under it
  const int16_t titleY = floorY + 30;    // FreeSansBold12 baseline
  const int16_t verY = floorY + 42;      // default font top
  const uint32_t DROP_MS = 650, TYPE_MS = 360, HOLD_MS = 650;
  char ver[48];
  snprintf(ver, sizeof(ver), "v%s  %s", FW_VERSION, BOARD_ID);
  const char *title = "espgotchi";
  const uint32_t t0 = millis();
  bool tunePlayed = false;
  for (;;) {
    uint32_t t = millis() - t0;
    hwLoop();
    gfx->fillScreen(C_INK);
    // Egg: ease-out bounce from above the screen down to the floor line.
    float u = t >= DROP_MS ? 1.0f : (float)t / DROP_MS;
    float b;  // 0 at start, 1 at rest, Penner's bounce
    if (u < 1 / 2.75f) b = 7.5625f * u * u;
    else if (u < 2 / 2.75f) { u -= 1.5f / 2.75f; b = 7.5625f * u * u + 0.75f; }
    else if (u < 2.5f / 2.75f) { u -= 2.25f / 2.75f; b = 7.5625f * u * u + 0.9375f; }
    else { u -= 2.625f / 2.75f; b = 7.5625f * u * u + 0.984375f; }
    int16_t eggY = (int16_t)(-eggSize + (floorY - eggSize + eggSize) * b);
    int16_t height = floorY - (eggY + eggSize);  // how far above the floor the egg is
    int16_t rx = max<int16_t>(6, eggSize / 2 - height / 6);
    gfx->fillEllipse(W / 2, floorY + 2, rx, 4, 0x2124);
    drawSprite(*sp.egg, eggX, eggY, scale);
    // Title types itself out once the egg has landed; the accent dot blinks like a cursor.
    if (t >= DROP_MS) {
      if (!tunePlayed) { hwTune(TUNE_BOOT); tunePlayed = true; }
      uint32_t tt = t - DROP_MS;
      size_t n = tt >= TYPE_MS ? strlen(title) : (size_t)(strlen(title) * tt / TYPE_MS);
      char shown[16];
      strncpy(shown, title, n); shown[n] = 0;
      gfx->setFont(&FreeSansBold12pt7b);
      int16_t tw = textWidth(title);
      text(W / 2 - tw / 2, titleY, shown, C_WHITE);
      if (n < strlen(title) && (tt / 90) % 2 == 0) gfx->fillRect(W / 2 - tw / 2 + textWidth(shown) + 2, titleY - 14, 3, 16, sp.accent);
      gfx->setFont(nullptr);
      if (tt >= TYPE_MS) {
        text(W / 2 - textWidth(ver) / 2, verY, ver, 0xBDF7);
        gfx->fillRect(W / 2 - 10, verY + 14, 20, 2, sp.accent);
      }
    }
    gfx->flush();
    if (t >= DROP_MS + TYPE_MS + HOLD_MS) break;
    delay(16);
  }
  gfx->setFont(nullptr);
}

// A mark every 8 px along the diagonal of each corner. On a panel with rounded corners of radius
// R, the mark at distance d from the corner is visible when d >= 0.293 R, so the first readable
// number gives R ~ 3.4 d.
void Ui::cornerTest() {
  gfx->fillScreen(C_INK);
  for (uint8_t i = 1; i <= 8; i++) {
    int16_t d = i * 8;
    uint16_t c = (i & 1) ? C_WARN : C_GOOD;
    char n[3];
    snprintf(n, sizeof(n), "%d", d);
    const int16_t corners[4][2] = {{d, d}, {W - 1 - d, d}, {d, H - 1 - d}, {W - 1 - d, H - 1 - d}};
    for (auto &pt : corners) {
      gfx->fillRect(pt[0] - 2, pt[1] - 2, 5, 5, c);
      text(pt[0] + (pt[0] < W / 2 ? 5 : -5 - textWidth(n)), pt[1] - 3, n, C_WHITE);
    }
  }
  gfx->drawRect(0, 0, W, H, C_BAD);
  gfx->flush();
}

void Ui::crosshair(int16_t x, int16_t y, uint8_t idx, uint8_t total) {
  gfx->fillScreen(C_INK);
  for (uint8_t i = 0; i < 4; i++) gfx->drawRect(i, i, W - 2 * i, H - 2 * i, C_BAD);  // the glass should show this frame on all four sides
  gfx->drawFastHLine(x - 12, y, 25, C_WARN);
  gfx->drawFastVLine(x, y - 12, 25, C_WARN);
  gfx->drawCircle(x, y, 6, C_WHITE);
  char buf[24];
  snprintf(buf, sizeof(buf), "tap the cross  %u/%u", idx + 1, total);
  text(W / 2 - textWidth(buf) / 2, H / 2 + 40, buf, 0xBDF7);
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

// Care actions waiting behind the current animation (Pet::queued()), drawn in the top bar
// band between `left` and `right`: a hollow "+N" pill in the accent color (hollow = not started
// yet, next to the solid stage pill). When the bar is crowded it degrades to a column of N
// dots, and to nothing rather than running into the name. nearLeft anchors it at `left`
// (landscape with the menu open, where there is no stage pill to sit next to).
void Ui::drawQueued(uint8_t n, int16_t left, int16_t right, bool nearLeft) {
  if (!n) return;
  if (n > PET_QUEUE_MAX) n = PET_QUEUE_MAX;
  char buf[4];
  snprintf(buf, sizeof(buf), "+%u", n);
  const int16_t w = textWidth(buf) + 12;
  if (right - left >= w) {
    const int16_t x = nearLeft ? left : right - w;
    gfx->drawRoundRect(x, 6, w, 16, 8, accent);
    text(x + 6, 10, buf, fg);
    return;
  }
  if (right - left < 5) return;
  const int16_t x = nearLeft ? left + 1 : right - 4, y0 = 14 - (n - 1) * 2;
  for (uint8_t i = 0; i < n; i++) gfx->fillCircle(x, y0 + i * 4, 1, accent);
}

// nameOnly: the landscape menu grid sits where the pill, battery and wifi dot go.
void Ui::drawTopBar(const Pet &pet, const NetInfo &net, bool nameOnly) {
  const PetState &s = pet.state();
  gfx->setFont(portrait ? &FreeSans9pt7b : &FreeSansBold12pt7b);
  const int16_t nameX = 8 + topInset;
  text(nameX, portrait ? 18 : 21, s.name, fg);
  const int16_t nameEnd = nameX + textWidth(s.name);
  gfx->setFont(nullptr);
  if (nameOnly) {
    drawQueued(pet.queued(), nameEnd + 8, panelX - 6, true);
    return;
  }
  // stage / age pill
  char buf[20];
  if (s.stage == STAGE_EGG) snprintf(buf, sizeof(buf), "egg  %lus", (unsigned long)s.eggSec);
  else if (s.ageSec < 3600) snprintf(buf, sizeof(buf), "%s  %lum", pet.stageName(), (unsigned long)(s.ageSec / 60));
  else snprintf(buf, sizeof(buf), "%s  %luh", pet.stageName(), (unsigned long)(s.ageSec / 3600));
  int16_t right = W - 22 - topInset;
  if (hwHasBattery()) {
    // battery outline with a fill proportional to the charge, left of the wifi dot. Low: the
    // whole icon blinks red at the 500 ms frame rate. Charging: a small bolt to its left.
    int pct = hwBatteryPct();
    const bool low = hwBatteryLow(), charging = hwBatteryCharging();
    const int16_t bx = right - 22, by = 8, bw = 18, bh = 10;
    const uint16_t outline = low && frame ? C_BAD : fg;
    gfx->drawRoundRect(bx, by, bw, bh, 2, outline);
    gfx->fillRect(bx + bw, by + 3, 2, 4, outline);
    int16_t fill = max<int16_t>((bw - 4) * pct / 100, low ? 1 : 0);  // an empty low cell still shows a sliver
    if (fill > 0 && !(low && !frame)) gfx->fillRect(bx + 2, by + 2, fill, bh - 4, low ? C_BAD : barColor(pct));
    right = bx - 6;
    if (charging) {
      const int16_t lx = bx - 9, ly = by - 1;  // 6x11 bolt: two triangles that overlap in the middle
      gfx->fillTriangle(lx + 4, ly, lx, ly + 6, lx + 4, ly + 6, C_WARN);
      gfx->fillTriangle(lx + 2, ly + 4, lx + 6, ly + 4, lx + 2, ly + 11, C_WARN);
      right = lx - 5;
    }
  }
  int16_t w = textWidth(buf) + 14;
  int16_t x = right - w;
  gfx->fillRoundRect(x, 6, w, 16, 8, accent);
  text(x + 7, 10, buf, C_INK);
  drawQueued(pet.queued(), nameEnd + 6, x - 5, false);
  // wifi dot
  uint16_t wc = net.connected ? C_GOOD : (net.apMode ? C_WARN : C_BAD);
  gfx->fillCircle(W - 11 - topInset, 14, 4, wc);
  if (!net.connected && frame) gfx->drawCircle(W - 11 - topInset, 14, 6, wc);
}

void Ui::drawRoomShell(bool night) {
  const int16_t rx = roomX, ry = roomY, rw = roomW, rh = roomH, floorY = ry + rh - 22;
  // Floor color first, then the wall on top with its bottom edge squared off, so the whole
  // room keeps one outline whatever its corner radius.
  gfx->fillRoundRect(rx, ry, rw, rh, roomR, mix(bg2, fg, 30));
  gfx->fillRoundRect(rx, ry, rw, floorY - ry, roomR, bg2);
  gfx->fillRect(rx, floorY - roomR, rw, roomR, bg2);
  if (night) {
    static const uint8_t stars[][2] = {{20, 14}, {60, 8}, {110, 20}, {150, 10}, {185, 26}, {90, 40}, {170, 48}};
    for (auto &st : stars) gfx->drawPixel(rx + st[0], ry + st[1], (frame ^ (st[0] & 1)) ? C_WHITE : muted);
  }
}

void Ui::drawRoom(const Pet &pet, bool night) {
  const PetState &s = pet.state();
  const int16_t rx = roomX, ry = roomY, rw = roomW, floorY = ry + roomH - 22;
  drawRoomShell(night);

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
    text(rx + max<int16_t>(portrait ? 4 : 14, edgeInset(H - floorY - 15)), floorY + 7, portrait ? "hold BOOT 6s: new egg" : "hold BOOT 6s for a new egg", muted);
    return;
  }
  if (s.stage == STAGE_EGG) {
    dx = frame ? 2 : -2;
    if (s.eggSec > 45) dy = frame ? -3 : 0;
    drawSprite(*spr, px + dx, py + dy, scale);
    text(rx + max<int16_t>(portrait ? 8 : 22, edgeInset(H - floorY - 15)), floorY + 7, portrait ? "BOOT: hatch early" : "press BOOT to hatch early", muted);
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
  const int16_t x = panelX, w = panelW;
  int16_t y = panelY;
  for (auto &r : rows) {
    text(x, y, r.label, muted);
    char v[6];
    snprintf(v, sizeof(v), "%d", r.v);
    text(x + w - textWidth(v), y, v, fg);
    drawBar(x, y + 10, w, 7, r.v);
    y += statRowH;
  }
}

void Ui::drawMenu(int8_t sel) {
  const Sprite *icons[MENU_COUNT] = {&SPR_MEAL_BURGER, &SPR_SNACK_COOKIE, &SPR_BALL,
#if HAS_TOUCH
                                     &SPR_ICON_PLAY,  // the ball itself is already the Play icon
#endif
                                     &SPR_HEART, &SPR_ICON_CLEAN, &SPR_ICON_SLEEP, &SPR_ICON_MEDS, &SPR_ICON_INFO};
  const char *labels[MENU_COUNT] = {"Feed", "Snack", "Play",
#if HAS_TOUCH
                                    "Game",
#endif
                                    "Pet", "Clean", "Lights", "Medicine", "Info"};
  int16_t cols, x0, y0;
  menuGrid(cols, x0, y0);
  const int16_t cell = MENU_CELL, gap = MENU_GAP;
  for (int i = 0; i < MENU_COUNT; i++) {
    int16_t cx = x0 + (i % cols) * (cell + gap), cy = y0 + (i / cols) * (cell + gap);
    gfx->fillRoundRect(cx, cy, cell, cell, 10, i == sel ? accent : mix(bg, fg, 25));
    drawSprite(*icons[i], cx + 6, cy + 6, 2);
  }
  int16_t rowsUsed = (MENU_COUNT + cols - 1) / cols;
  // Landscape has no room under the grid: label + hint go on the room floor strip instead
  const int16_t floorY = roomY + roomH - 22;
  const int16_t lx = portrait ? x0 : roomX + 10 + edgeInset(H - floorY - 16), ly = portrait ? y0 + rowsUsed * (cell + gap) + 12 : floorY + 16;
  gfx->setFont(&FreeSans9pt7b);
  text(lx, ly, labels[sel], fg);
  const int16_t labelW = textWidth(labels[sel]);
  gfx->setFont(nullptr);
  const char *hint = HAS_TOUCH ? "tap an icon, or hold BOOT" : "hold BOOT to select";
  if (portrait) text(max<int16_t>(x0, edgeInset(10)), H - 10, hint, muted);
  else {
    // Right of the label on the floor strip when it fits (wide room), else up in the sky.
    int16_t hx = roomX + roomW - 8 - textWidth(hint);
    if (hx > lx + labelW + 12) text(hx, floorY + 8, hint, muted);
    else text(roomX + 10, roomY + 8, hint, muted);
  }
}

// Two columns over the stats panel (landscape) or two rows under the room (portrait): 2x4 / 4x2
// with eight items, 2x5 / 5x2 with the touch boards' ninth.
void Ui::menuGrid(int16_t &cols, int16_t &x0, int16_t &y0) const {
  cols = portrait ? (MENU_COUNT + 1) / 2 : 2;
  x0 = portrait ? (W - (cols * MENU_CELL + (cols - 1) * MENU_GAP)) / 2 : panelX;
  y0 = portrait ? panelY : (LCD_CORNER_RADIUS ? 12 : 4);  // rounded glass: clear the top corner
}

int8_t Ui::menuHit(int16_t x, int16_t y) const {
  int16_t cols, x0, y0;
  menuGrid(cols, x0, y0);
  const int16_t pitch = MENU_CELL + MENU_GAP, slack = 4;  // a finger is not a pixel
  if (x < x0 - slack || y < y0 - slack) return -1;
  int16_t c = (x - x0) / pitch, r = (y - y0) / pitch;
  if (c >= cols) return -1;
  int16_t i = r * cols + c;
  return i < MENU_COUNT ? (int8_t)i : -1;
}

void Ui::drawInfo(const Pet &pet, const NetInfo &net) {
  const PetState &s = pet.state();
  gfx->fillScreen(C_INK);
  const int16_t lx = 10 + edgeInset(8);  // rounded glass: keep the text block clear of the corners
  gfx->setFont(&FreeSansBold12pt7b);
  text(lx, 22, s.name, C_WHITE);
  gfx->setFont(nullptr);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s  gen %u", SPECIES[s.species < SPECIES_COUNT ? s.species : 0].label, s.generation);
  if (portrait) text(lx, 30, buf, 0xBDF7);
  else text(W - lx - 2 - textWidth(buf), 10, buf, 0xBDF7);
  int16_t y = portrait ? 48 : 38;
  auto line = [&](uint16_t color, const char *fmt, auto... args) {
    gfx->setCursor(lx, y);
    gfx->setTextColor(color);
    gfx->printf(fmt, args...);
    y += 13;
  };
  if (portrait || W < 300) {  // narrow screens: one fact per line
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
    line(C_WHITE, "http://%s.local/", net.host);
  } else if (net.apMode) {
    line(C_WARN, "Setup network: %s", net.ssid);
    line(C_WHITE, "Open network, no password");
    line(C_WHITE, portrait ? "Join it, setup page opens" : "Join it and the setup page pops up");
  } else {
    line(C_WARN, "WiFi connecting...");
  }
  y += 8;
  line(0xBDF7, "Time %s  FW %s  heap %luk", net.timeValid ? "ok" : "no", FW_VERSION, (unsigned long)(ESP.getFreeHeap() / 1024));
  if (hwHasBattery()) line(hwBatteryLow() ? C_BAD : 0xBDF7, "Battery %d%%  %d.%02d V%s%s", hwBatteryPct(), hwBatteryMv() / 1000, (hwBatteryMv() % 1000) / 10, hwBatteryLow() ? "  low" : "", hwBatteryCharging() ? "  charging" : "");
  // Credits pinned to the bottom, above the back hint, so they sit in the same place on every board.
  const int16_t fx = 10 + edgeInset(22);
  const char *follow = "Follow for more  ";
  text(fx, H - 22, follow, 0xBDF7);
  text(fx + textWidth(follow), H - 22, "adrianmb.dev", SPECIES[s.species < SPECIES_COUNT ? s.species : 0].accent);
  text(lx, H - 10, HAS_TOUCH ? "tap or press BOOT to go back" : "press BOOT to go back", 0x7BEF);
}

#if HAS_TOUCH
void Ui::gameArena(int16_t &x, int16_t &y, int16_t &w, int16_t &h) const {
  const int16_t floorY = roomY + roomH - 22, pad = 4;
  // The room hugs the glass, so the corner can eat its top and bottom edges: keep the ball off them.
  const int16_t side = max<int16_t>(pad, max<int16_t>(edgeInset(roomY), edgeInset(H - floorY)) - roomX + pad);
  x = roomX + side; w = roomW - 2 * side - GAME_BALL_PX;
  y = roomY + pad; h = floorY + 4 - GAME_BALL_PX - y;  // the ball rests on the same line as the pet's feet
}

// Catch the ball: the room with the pet watching (hopping on a catch), the ball on top, and the
// score and clock where the stats normally go. When the round is over a card shows the score.
void Ui::renderGame(const Pet &pet, const GameView &g, bool night) {
  uint32_t now = millis();
  if (now - frameMs >= 500) { frameMs = now; frame ^= 1; }
  pickTheme(pet, night);
  gfx->fillScreen(bg);
  gfx->setFont(portrait ? &FreeSans9pt7b : &FreeSansBold12pt7b);
  text(8 + topInset, portrait ? 18 : 21, "Catch the ball", fg);
  gfx->setFont(nullptr);
  drawRoomShell(night);
  const PetState &s = pet.state();
  const SpeciesInfo &sp = SPECIES[s.species < SPECIES_COUNT ? s.species : 0];
  const uint8_t scale = 4;
  const int16_t floorY = roomY + roomH - 22;
  const int16_t px = roomX + (roomW - 24 * scale) / 2, py = floorY + 4 - 24 * scale;
  const int16_t dy = g.hop ? -16 : 0;  // same jump as the Play animation
  gfx->fillRoundRect(px + 20, floorY - 1, 56, 6, 3, mix(bg2, fg, 60));
  drawSprite(*sp.frames[pet.spriteSlot()][frame ? 1 : 0], px, py + dy, scale);
  if (!g.over) drawSprite(SPR_BALL, g.ballX, g.ballY, GAME_BALL_SCALE);

  // HUD on the panel: landscape stacks score over time, portrait puts them side by side.
  char buf[8];
  auto stat = [&](int16_t x, int16_t y, const char *label, uint8_t v) {
    text(x, y, label, muted);
    snprintf(buf, sizeof(buf), "%u", v);
    gfx->setFont(&FreeSansBold12pt7b);
    text(x, y + 28, buf, fg);
    gfx->setFont(nullptr);
  };
  if (portrait) {
    stat(panelX, panelY, "SCORE", g.score);
    stat(panelX + panelW / 2, panelY, "TIME", g.secondsLeft);
  } else {
    stat(panelX, panelY, "SCORE", g.score);
    stat(panelX, panelY + 2 * statRowH, "TIME", g.secondsLeft);
  }
  // Hint under the grid in portrait; landscape has no room there, so it goes on the floor strip.
  const char *hint = g.over ? "tap or press BOOT" : "tap the ball";  // short enough for the 160 px landscape floor
  if (portrait) text(max<int16_t>(panelX, edgeInset(10)), H - 10, hint, muted);
  else text(roomX + 10 + edgeInset(H - floorY - 16), floorY + 8, hint, muted);

  if (g.over) {  // score card over the room
    snprintf(buf, sizeof(buf), "Score %u", g.score);
    gfx->setFont(&FreeSansBold12pt7b);
    const int16_t tw = textWidth(buf), cw = tw + 28, ch = 36;
    const int16_t cx = roomX + (roomW - cw) / 2, cy = roomY + (roomH - ch) / 2;
    gfx->fillRoundRect(cx, cy, cw, ch, 10, accent);
    text(cx + 14, cy + 25, buf, C_INK);
    gfx->setFont(nullptr);
  }
  gfx->flush();
}
#endif

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
  drawTopBar(pet, net, menuSel >= 0 && !portrait);
  drawRoom(pet, night);
  if (menuSel >= 0) drawMenu(menuSel);
  else drawStats(pet);
  gfx->flush();
}
