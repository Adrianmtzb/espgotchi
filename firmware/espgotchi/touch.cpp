#include "touch.h"
#include <Wire.h>
#include "config.h"

#if HAS_TOUCH
static const uint8_t REG_FINGER = 0x02, REG_XH = 0x03, REG_CHIP_ID = 0xA7, REG_DIS_AUTOSLEEP = 0xFE;
static const uint16_t TAP_MAX_MS = 400, SWIPE_MIN_PX = 40, TAP_MAX_MOVE = 18, POLL_MS = 15;

static bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_I2C_ADDR, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool Touch::begin(uint8_t rot) {
  rotation = rot & 3;
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(60);
  uint8_t id = 0;
  ok = readRegs(REG_CHIP_ID, &id, 1);
  if (!ok) { Serial.println("[touch] CST816 not found"); return false; }
  Serial.printf("[touch] CST816 chip id 0x%02X\n", id);  // B4 = S, B5 = T, B6 = D
  writeReg(REG_DIS_AUTOSLEEP, 0xFF);  // stay awake so polling keeps working
  return true;
}

// Native panel coordinates (portrait, 240x280, origin top-left) to the rotated canvas.
void Touch::toCanvas(int16_t nx, int16_t ny, int16_t &x, int16_t &y) const {
  switch (rotation) {
    case 0: x = nx; y = ny; break;
    case 1: x = ny; y = LCD_NATIVE_W - 1 - nx; break;
    case 2: x = LCD_NATIVE_W - 1 - nx; y = LCD_NATIVE_H - 1 - ny; break;
    default: x = LCD_NATIVE_H - 1 - ny; y = nx; break;
  }
}

bool Touch::readPoint(int16_t &x, int16_t &y, int16_t &nx, int16_t &ny) {
  uint8_t b[5];
  if (!readRegs(REG_FINGER, b, sizeof(b))) return false;
  // A failed or empty I2C read comes back as 0xFF bytes: finger count 15 at (4095, 4095).
  // Those produced phantom taps on the real board, so anything implausible is dropped here.
  uint8_t fingers = b[0] & 0x0F;
  if (fingers == 0 || fingers > 2) return false;
  nx = ((b[1] & 0x0F) << 8) | b[2]; ny = ((b[3] & 0x0F) << 8) | b[4];
  if (nx >= 0x0FFF || ny >= 0x0FFF) return false;
  // Board calibration (see boards/*.h), then clamp to the panel.
  int16_t px = constrain((int)(nx * TOUCH_X_GAIN + TOUCH_X_OFFSET), 0, LCD_NATIVE_W - 1);
  int16_t py = constrain((int)(ny * TOUCH_Y_GAIN + TOUCH_Y_OFFSET), 0, LCD_NATIVE_H - 1);
  toCanvas(px, py, x, y);
  return true;
}

TouchEvent Touch::poll() {
  TouchEvent e;
  if (!ok) return e;
  uint32_t now = millis();
  if (now - lastPollMs < POLL_MS) return e;
  lastPollMs = now;
  int16_t x, y, nx, ny;
  bool touching = readPoint(x, y, nx, ny);
  // Debounce: a finger has to be seen on two consecutive polls before it counts as down.
  if (touching && !isDown && !armed) { armed = true; return e; }
  if (!touching) armed = false;
  if (touching && !isDown) {
    isDown = true; longFired = false; downMs = now;
    x0 = xl = x; y0 = yl = y; rx0 = nx; ry0 = ny;
  } else if (touching) {
    xl = x; yl = y;
    if (!longFired && now - downMs >= BUTTON_LONG_PRESS_MS && abs(xl - x0) < TAP_MAX_MOVE && abs(yl - y0) < TAP_MAX_MOVE) {
      longFired = true;
      e.kind = TouchEvent::LONG_PRESS; e.x = x0; e.y = y0; e.rawX = rx0; e.rawY = ry0;
    }
  } else if (isDown) {
    isDown = false;
    if (longFired) return e;
    int16_t dx = xl - x0, dy = yl - y0;
    e.x = x0; e.y = y0; e.rawX = rx0; e.rawY = ry0;
    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy)) e.kind = dx > 0 ? TouchEvent::SWIPE_RIGHT : TouchEvent::SWIPE_LEFT;
    else if (abs(dy) >= SWIPE_MIN_PX) e.kind = dy > 0 ? TouchEvent::SWIPE_DOWN : TouchEvent::SWIPE_UP;
    else if (now - downMs <= TAP_MAX_MS && abs(dx) < TAP_MAX_MOVE && abs(dy) < TAP_MAX_MOVE) e.kind = TouchEvent::TAP;
  }
  return e;
}
#else
bool Touch::begin(uint8_t) { return false; }
TouchEvent Touch::poll() { return TouchEvent(); }
bool Touch::readPoint(int16_t &, int16_t &, int16_t &, int16_t &) { return false; }
void Touch::toCanvas(int16_t, int16_t, int16_t &, int16_t &) const {}
#endif
