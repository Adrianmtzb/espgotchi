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

#if HAS_BATTERY
// Battery monitor. One filtered reading per second; the percentage, the low flag and the
// charging guess all come from the same filtered value so they never disagree with each other.
static int batMv = -1;               // filtered millivolts
static uint32_t batNextSampleMs = 0;
static bool batLow = false, batCharging = false, batAlarmPending = false;
static uint32_t batAlarmMs = 0;      // when the low alarm last fired
// 60 s trend window: one slot per BATTERY_TREND_STEP_MS, the oldest slot is 60 s behind.
static const uint8_t BAT_TREND_N = BATTERY_TREND_WINDOW_MS / BATTERY_TREND_STEP_MS;
static int16_t batTrend[BAT_TREND_N];
static uint8_t batTrendHead = 0, batTrendFill = 0;
static uint32_t batTrendNextMs = 0;
static uint32_t batRiseSeenMs = 0;   // last time the 60 s delta cleared the charging threshold

static int batteryRead() {
  // Average a few samples: the divider sits next to the WiFi radio and the ADC is noisy.
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
  return (int)(sum / 8 * BATTERY_DIVIDER);
}

static int batteryPctFromMv(int mv) {
  // Piecewise LiPo discharge curve (open-circuit voltage, rough but monotonic).
  static const struct { uint16_t mv; uint8_t pct; } curve[] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 62}, {3800, 45}, {3700, 25}, {3600, 10}, {3500, 4}, {3300, 0}};
  if (mv >= curve[0].mv) return 100;
  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv >= curve[i].mv) {
      int span = curve[i - 1].mv - curve[i].mv;
      return curve[i].pct + (mv - curve[i].mv) * (curve[i - 1].pct - curve[i].pct) / span;
    }
  }
  return 0;
}

static void batterySample() {
  uint32_t now = millis();
  batNextSampleMs = now + 1000;
  int raw = batteryRead();
  batMv = batMv < 0 ? raw : (batMv * 3 + raw) / 4;  // light IIR on top of the 8-sample average

  // Low flag with hysteresis so a noisy reading around the threshold does not flap the icon.
  int pct = batteryPctFromMv(batMv);
  if (!batLow && pct <= BATTERY_LOW_PCT) { batLow = true; batAlarmPending = true; batAlarmMs = now; }
  else if (batLow && pct >= BATTERY_LOW_CLEAR_PCT) batLow = false;
  else if (batLow && (uint32_t)(now - batAlarmMs) >= BATTERY_LOW_REMIND_MS) { batAlarmPending = true; batAlarmMs = now; }

  // Charging guess: the pack voltage climbing >= BATTERY_CHARGE_RISE_MV over the last 60 s.
  // A full pack on USB sits flat at ~4.2 V, so this only catches an actual charge in progress.
  if ((int32_t)(now - batTrendNextMs) >= 0) {
    batTrendNextMs = now + BATTERY_TREND_STEP_MS;
    batTrend[batTrendHead] = (int16_t)batMv;
    batTrendHead = (batTrendHead + 1) % BAT_TREND_N;
    if (batTrendFill < BAT_TREND_N) batTrendFill++;
    if (batTrendFill == BAT_TREND_N) {
      int oldest = batTrend[batTrendHead];  // the slot about to be overwritten is the oldest
      if (batMv - oldest >= BATTERY_CHARGE_RISE_MV) batRiseSeenMs = now;
    }
  }
  // Stay "charging" for one window after the last rise, so the bolt does not flicker between steps.
  batCharging = batRiseSeenMs && (uint32_t)(now - batRiseSeenMs) < BATTERY_TREND_WINDOW_MS;
  if (!batCharging) batRiseSeenMs = 0;
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
  // no VBUS sense on this board: charging is inferred from the voltage trend in batterySample()
  batterySample();
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
#if HAS_BATTERY
  if ((int32_t)(millis() - batNextSampleMs) >= 0) batterySample();
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
TUNE(T_POOP, 70, 196, 165, 0, 131, 110, 98);                  // a sliding, embarrassed plop
TUNE(T_LOWBAT, 90, 988, 0, 659);                              // two short notes going down: feed me power
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
    case TUNE_POOP: PLAY(T_POOP); break;
    case TUNE_LOWBAT: PLAY(T_LOWBAT); break;
  }
#else
  (void)t;
#endif
}

bool hwHasBattery() { return HAS_BATTERY != 0; }

int hwBatteryMv() {
#if HAS_BATTERY
  if (batMv < 0) batterySample();
  return batMv;
#else
  return -1;
#endif
}

int hwBatteryPct() {
#if HAS_BATTERY
  return batteryPctFromMv(hwBatteryMv());
#else
  return -1;
#endif
}

bool hwBatteryLow() {
#if HAS_BATTERY
  return batLow;
#else
  return false;
#endif
}

bool hwBatteryCharging() {
#if HAS_BATTERY
  return batCharging;
#else
  return false;
#endif
}

bool hwBatteryLowAlarmDue() {
#if HAS_BATTERY
  bool due = batAlarmPending;
  batAlarmPending = false;
  return due;
#else
  return false;
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
