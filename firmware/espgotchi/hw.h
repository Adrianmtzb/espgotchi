#pragma once
// Board extras behind one interface: the mood LED (C6), the buzzer, the battery gauge and the
// power latch (S3). Every call is a no-op on a board without that part, so the sketch never
// needs to know which board it runs on.
#include <Arduino.h>

void hwBegin();
void hwLoop();
void hwLed(uint8_t r, uint8_t g, uint8_t b);
void hwBeep(uint16_t hz, uint16_t ms);  // non-blocking, replaces any beep in progress
// Short tunes, one per thing that can happen. Non-blocking; a new tune replaces the current one.
enum Tune : uint8_t { TUNE_TICK, TUNE_OK, TUNE_NO, TUNE_FEED, TUNE_SNACK, TUNE_YUM, TUNE_PLAY, TUNE_PET, TUNE_CLEAN,
                      TUNE_SLEEP, TUNE_WAKE, TUNE_MEDS, TUNE_INFO, TUNE_JINGLE, TUNE_BOOT, TUNE_SAD, TUNE_POWEROFF, TUNE_POOP,
                      TUNE_SNORE, TUNE_SNEEZE, TUNE_HELLO };
void hwTune(Tune t);
bool hwTunePlaying();  // true while a beep or tune is still sounding; lets ambient sounds wait their turn
inline void hwJingle() { hwTune(TUNE_JINGLE); }  // hatch, evolution, new egg
bool hwHasBattery();
int hwBatteryMv();   // -1 without a gauge
int hwBatteryPct();  // 0-100, -1 without a gauge
bool hwPowerKeyHeld();
void hwPowerOff();   // cuts the battery rail; does nothing while on USB
