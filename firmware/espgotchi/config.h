#pragma once
// Waveshare ESP32-C6-LCD-1.47 pinout (from the official wiki + schematic)
#define PIN_LCD_MOSI 6
#define PIN_LCD_SCLK 7
#define PIN_LCD_CS 14
#define PIN_LCD_DC 15
#define PIN_LCD_RST 21
#define PIN_LCD_BL 22
#define PIN_SD_MISO 5
#define PIN_SD_CS 4
#define PIN_RGB_LED 8   // WS2812B
#define PIN_BOOT_BTN 9  // "BOOT" key, active low, 10K pull-up on board

// Panel: ST7789, 172x320 native, column offset 34. We use it in landscape.
#define LCD_NATIVE_W 172
#define LCD_NATIVE_H 320
#define LCD_COL_OFFSET 34
#define SCREEN_W 320
#define SCREEN_H 172

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
