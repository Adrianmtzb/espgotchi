# Waveshare ESP32-C6-LCD-1.47 — ficha de hardware

Wiki: https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47
Esquemático: https://files.waveshare.com/wiki/ESP32-C6-LCD-1.47/ESP32-C6-LCD-1.47_schemetics.pdf
Demo oficial (Arduino + ESP-IDF): https://files.waveshare.com/wiki/ESP32-C6-LCD-1.47/ESP32-C6-LCD-1.47-Demo.zip

## MCU
- ESP32-C6FH4: RISC-V a 160 MHz, 4 MB flash integrado, ~320 KB SRAM usable, sin PSRAM.
- WiFi 6 (2,4 GHz), BLE 5, 802.15.4 (Zigbee/Thread).
- USB-C al USB-Serial-JTAG nativo del chip. En macOS aparece como `/dev/cu.usbmodem*`.

## Pantalla
- 1,47" IPS TFT, controlador ST7789, 172×320, 262K colores, SPI hasta 80 MHz. Sin táctil.
- Offset de columna 34 (la RAM del ST7789 es 240 de ancho). Inversión de color activada (INVON 0x21).
- Backlight: MOSFET SI2302 en GPIO 22, activo alto, admite PWM.

| Señal | GPIO |
|---|---|
| LCD MOSI (SDA) | 6 |
| LCD SCLK (SCL) | 7 |
| LCD CS | 14 |
| LCD DC | 15 |
| LCD RST | 21 |
| LCD BL | 22 |

## Otros periféricos
| Periférico | GPIO | Notas |
|---|---|---|
| microSD CS | 4 | SPI compartido con la LCD (MOSI 6, SCLK 7) |
| microSD MISO | 5 | |
| LED RGB WS2812B | 8 | `rgbLedWrite(8, r, g, b)` en core esp32 3.x |
| Botón BOOT | 9 | activo bajo, pull-up 10K en placa; único botón programable |
| Botón RST | — | reset del chip, no programable |
| GPIO libres en header | 0, 1, 2, 3, 18, 19, 20, 23 | más TXD/RXD (UART0), 3V3, 5V, GND |

## Toolchain probado
- `arduino-cli` con core `esp32:esp32` 3.2.0.
- FQBN: `esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M` (2 MB para la app).
- Gráficos: `GFX Library for Arduino` (Arduino_GFX) 1.6.4:
  ```cpp
  Arduino_DataBus *bus = new Arduino_ESP32SPI(15 /*DC*/, 14 /*CS*/, 7 /*SCK*/, 6 /*MOSI*/, GFX_NOT_DEFINED, FSPI);
  Arduino_GFX *gfx = new Arduino_ST7789(bus, 21 /*RST*/, 1 /*rot*/, true /*ips*/, 172, 320, 34, 0, 34, 0);
  ledcAttach(22, 1000, 8); ledcWrite(22, 200);  // backlight
  ```
- Alternativa oficial de Waveshare: LVGL 8.3.10 + PNGdec 1.0.2, o ESP-IDF.
- Flasheo: `arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn <FQBN> <sketch>`. Si falla, mantener BOOT, pulsar RST, soltar BOOT.
