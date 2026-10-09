#pragma once
#include <sdkconfig.h>  // CONFIG_IDF_TARGET_* comes from the core

// Board selection: one header per supported board, picked by the compiler target so the same
// sketch builds for every board with nothing more than the right FQBN (see the Makefile).
#if defined(CONFIG_IDF_TARGET_ESP32C6)
#include "boards/c6_lcd147.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "boards/s3_touch169.h"
#else
#error "Unsupported target: add a header under boards/ and select it here"
#endif

// Offsets for the mirrored rotations default to the plain ones unless the board says otherwise.
#ifndef LCD_COL_OFFSET2
#define LCD_COL_OFFSET2 LCD_COL_OFFSET
#endif
#ifndef LCD_ROW_OFFSET2
#define LCD_ROW_OFFSET2 LCD_ROW_OFFSET
#endif

// Logical screen size in landscape (rotation 1). Portrait swaps them at runtime.
#define SCREEN_W LCD_NATIVE_H
#define SCREEN_H LCD_NATIVE_W

#define FW_VERSION "0.1.0"
#define DEFAULT_PET_NAME "Pixel"
#define MDNS_HOST "espgotchi"  // -> http://espgotchi.local
#define AP_SSID_PREFIX "ESPgotchi-"
#define DEFAULT_TZ "CST6"  // America/Mexico_City (no DST). Change via web, API or CLI "tz"
#define NTP_SERVER "pool.ntp.org"

#define BUTTON_DOUBLE_CLICK_MS 350
#define BUTTON_LONG_PRESS_MS 600
#define BUTTON_RESET_PRESS_MS 6000
#define MENU_TIMEOUT_MS 6000
#define INFO_TIMEOUT_MS 10000
#define SAVE_INTERVAL_MS 60000
#define BACKLIGHT_DAY 220
#define BACKLIGHT_NIGHT 40
#define LED_MAX_BRIGHTNESS 40  // 0-255, keep it gentle

// Battery monitor (boards with HAS_BATTERY). Percent thresholds carry hysteresis so the icon
// does not flap around the limit; the alarm repeats while the pack stays low.
#define BATTERY_LOW_PCT 15
#define BATTERY_LOW_CLEAR_PCT 20
#define BATTERY_LOW_REMIND_MS (10UL * 60 * 1000)
#define BATTERY_LOW_BACKLIGHT_PCT 60      // cap the backlight to this % of the configured brightness
#define BATTERY_CHARGE_RISE_MV 40         // voltage climb over the trend window that reads as "charging"
#define BATTERY_TREND_WINDOW_MS 60000
#define BATTERY_TREND_STEP_MS 5000

// Visits between boards on the same LAN (mDNS _espgotchi._tcp). A board alone finds nobody and stays quiet.
#define VISIT_INTERVAL_MS 300000UL  // how often to look for a neighbour
#define VISIT_FIRST_MS 60000UL      // first look after the WiFi comes up
#define VISIT_DURATION_MS 20000UL   // how long the friend stays in the room
#define VISIT_HTTP_TIMEOUT_MS 1500  // per fetch; a C6 needs ~300 ms to serve /api/state, 400 ms timed out on the LAN
