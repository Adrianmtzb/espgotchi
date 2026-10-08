#include "hw.h"
#include "config.h"

#if HAS_BUZZER
static uint32_t toneUntil = 0;
static const uint16_t *seq = nullptr;
static uint8_t seqLen = 0, seqPos = 0, seqStep = 0;
static void toneNow(uint16_t hz, uint16_t ms) {
  if (hz) ledcWriteTone(PIN_BUZZER, hz);
  else ledcWrite(PIN_BUZZER, 0);
  toneUntil = millis() + ms;
}
#endif

void hwBegin() {
#if HAS_POWER_LATCH
  // Hold the battery rail on. On USB the regulator is powered anyway, so this is harmless there.
  pinMode(PIN_SYS_EN, OUTPUT);
  digitalWrite(PIN_SYS_EN, HIGH);
  pinMode(PIN_SYS_OUT, INPUT);
#endif
#if HAS_BUZZER
  // Explicit channel: channels 2n and 2n+1 share a timer, and ledcWriteTone retunes the timer.
  // The backlight sits on channel 0 (timer 0), so the buzzer takes channel 2 (timer 1).
  if (!ledcAttachChannel(PIN_BUZZER, 2000, 10, 2)) Serial.println("[hw] buzzer PWM attach failed");
  ledcWrite(PIN_BUZZER, 0);
#endif
#if HAS_BATTERY
  analogReadResolution(12);
  pinMode(PIN_BAT_ADC, INPUT);
#endif
#if HAS_RGB_LED
  rgbLedWrite(PIN_RGB_LED, 0, 0, 0);
#endif
}

void hwLoop() {
#if HAS_BUZZER
  if (toneUntil && (int32_t)(millis() - toneUntil) >= 0) {
    if (seq && seqPos < seqLen) toneNow(seq[seqPos++], seqStep);
    else { ledcWrite(PIN_BUZZER, 0); toneUntil = 0; seq = nullptr; }
  }
#endif
}

void hwLed(uint8_t r, uint8_t g, uint8_t b) {
#if HAS_RGB_LED
  rgbLedWrite(PIN_RGB_LED, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}

void hwBeep(uint16_t hz, uint16_t ms) {
#if HAS_BUZZER
  seq = nullptr;
  toneNow(hz, ms);
#else
  (void)hz; (void)ms;
#endif
}

// Note tables (Hz, 0 = rest). Each tune has one step length so the sequencer stays trivial.
#if HAS_BUZZER
#define TUNE(name, step, ...) static const uint16_t name[] = {__VA_ARGS__}; static const uint8_t name##_LEN = sizeof(name) / sizeof(name[0]); static const uint16_t name##_STEP = step
TUNE(T_TICK, 25, 2400);                                       // menu step
TUNE(T_OK, 60, 1760);                                         // generic accept
TUNE(T_NO, 90, 220, 0, 196);                                  // refused: two low grumbles
TUNE(T_FEED, 45, 392, 294);                                   // one bite: a low chomp, played once per eating frame
TUNE(T_SNACK, 30, 1047, 784, 1319);                           // one crunch, played once per eating frame
TUNE(T_YUM, 80, 523, 659, 784);                               // swallowed, satisfied
TUNE(T_PLAY, 55, 659, 784, 988, 784, 659, 784, 988, 1175);    // bouncing ball
TUNE(T_PET, 110, 880, 1047, 0, 1319);                         // soft, warm
TUNE(T_CLEAN, 35, 1047, 1175, 1319, 1397, 1568, 1760, 1976, 2093);  // bubbles rising
TUNE(T_SLEEP, 160, 784, 659, 523);                            // good night, going down
TUNE(T_WAKE, 110, 523, 659, 784);                             // morning, going up
TUNE(T_MEDS, 120, 988, 0, 988, 0, 1319);                      // two pills and relief
TUNE(T_INFO, 40, 1568, 2093);                                 // page flip
TUNE(T_JINGLE, 90, 1047, 1319, 1568, 2093);                   // C6 E6 G6 C7: hatch, evolution
TUNE(T_BOOT, 70, 523, 659, 784, 0, 1047);                     // power on: C E G, rest, high C
TUNE(T_SAD, 220, 392, 349, 311, 262);                         // death
TUNE(T_POWEROFF, 120, 784, 523, 392);                         // shutting down
#undef TUNE
#define PLAY(name) do { seq = name; seqLen = name##_LEN; seqPos = 1; seqStep = name##_STEP; toneNow(name[0], seqStep); } while (0)
#endif

void hwTune(Tune t) {
#if HAS_BUZZER
  switch (t) {
    case TUNE_TICK: PLAY(T_TICK); break;
    case TUNE_OK: PLAY(T_OK); break;
    case TUNE_NO: PLAY(T_NO); break;
    case TUNE_FEED: PLAY(T_FEED); break;
    case TUNE_SNACK: PLAY(T_SNACK); break;
    case TUNE_YUM: PLAY(T_YUM); break;
    case TUNE_PLAY: PLAY(T_PLAY); break;
    case TUNE_PET: PLAY(T_PET); break;
    case TUNE_CLEAN: PLAY(T_CLEAN); break;
    case TUNE_SLEEP: PLAY(T_SLEEP); break;
    case TUNE_WAKE: PLAY(T_WAKE); break;
    case TUNE_MEDS: PLAY(T_MEDS); break;
    case TUNE_INFO: PLAY(T_INFO); break;
    case TUNE_JINGLE: PLAY(T_JINGLE); break;
    case TUNE_BOOT: PLAY(T_BOOT); break;
    case TUNE_SAD: PLAY(T_SAD); break;
    case TUNE_POWEROFF: PLAY(T_POWEROFF); break;
  }
#else
  (void)t;
#endif
}

bool hwHasBattery() { return HAS_BATTERY != 0; }

int hwBatteryMv() {
#if HAS_BATTERY
  // Average a few samples: the divider sits next to the WiFi radio and the ADC is noisy.
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
  return (int)(sum / 8 * BATTERY_DIVIDER);
#else
  return -1;
#endif
}

int hwBatteryPct() {
#if HAS_BATTERY
  // Piecewise LiPo discharge curve (open-circuit voltage, rough but monotonic).
  static const struct { uint16_t mv; uint8_t pct; } curve[] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 62}, {3800, 45}, {3700, 25}, {3600, 10}, {3500, 4}, {3300, 0}};
  int mv = hwBatteryMv();
  if (mv >= curve[0].mv) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv >= curve[i].mv) {
      int span = curve[i - 1].mv - curve[i].mv;
      return curve[i].pct + (mv - curve[i].mv) * (curve[i - 1].pct - curve[i].pct) / span;
    }
  }
  return 0;
#else
  return -1;
#endif
}

bool hwPowerKeyHeld() {
#if HAS_POWER_LATCH
  return digitalRead(PIN_SYS_OUT) == LOW;  // measured: idles HIGH on the real board, so pressed is LOW
#else
  return false;
#endif
}

void hwPowerOff() {
#if HAS_POWER_LATCH
  ledcWrite(PIN_LCD_BL, 0);
  digitalWrite(PIN_SYS_EN, LOW);
#endif
}
