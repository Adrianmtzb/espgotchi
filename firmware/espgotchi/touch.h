#pragma once
// CST816 capacitive touch (I2C 0x15) reduced to the gestures the pet needs. Gestures are
// derived from polled positions rather than the chip's gesture register, which only reports
// reliably in interrupt mode. Coordinates come out already rotated to the canvas.
#include <Arduino.h>

struct TouchEvent {
  enum Kind : uint8_t { NONE, TAP, LONG_PRESS, SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN } kind = NONE;
  int16_t x = 0, y = 0;  // where the finger went down, canvas coordinates
  int16_t rawX = 0, rawY = 0;  // controller coordinates of the same point, for bring-up logs
};

class Touch {
 public:
  bool begin(uint8_t rotation);
  TouchEvent poll();
  bool present() const { return ok; }
  bool down() const { return isDown; }

 private:
  bool ok = false, isDown = false, longFired = false, armed = false;
  uint8_t rotation = 1;
  uint32_t lastPollMs = 0, downMs = 0;
  int16_t x0 = 0, y0 = 0, xl = 0, yl = 0, rx0 = 0, ry0 = 0;
  bool readPoint(int16_t &x, int16_t &y, int16_t &nx, int16_t &ny);
  void toCanvas(int16_t nx, int16_t ny, int16_t &x, int16_t &y) const;
};
