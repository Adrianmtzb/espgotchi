#pragma once
// Waveshare ESP32-C6-LCD-1.47 (pinout from the official wiki + schematic).
// Selected automatically when the sketch is compiled for the esp32c6 target.
#define BOARD_NAME "Waveshare ESP32-C6-LCD-1.47"
#define BOARD_ID "c6-lcd147"

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

// Panel: ST7789, 172x320 native, column offset 34 (the controller RAM is 240 wide).
#define LCD_NATIVE_W 172
#define LCD_NATIVE_H 320
#define LCD_COL_OFFSET 34
#define LCD_ROW_OFFSET 0
#define LCD_CORNER_RADIUS 0  // square glass

#define HAS_RGB_LED 1
#define HAS_TOUCH 0
#define HAS_BUZZER 0
#define HAS_BATTERY 0
#define HAS_POWER_LATCH 0
