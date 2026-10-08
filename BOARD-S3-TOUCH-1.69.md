# Waveshare ESP32-S3-Touch-LCD-1.69 — ficha de hardware

Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69 (pines en https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69)
Esquemático: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69/ESP32-S3-Touch-LCD-1.69-Sch.pdf
Demo oficial: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69/ESP32-S3-Touch-LCD-1.69_Demo.zip

Segunda placa soportada por ESPgotchi (`make build BOARD=s3`). La ficha de la primera está en
[BOARD.md](BOARD.md). El pinout sale de la documentación oficial y está verificado en una placa
real (revisión actual, octubre de 2026): pantalla, táctil, zumbador, batería y botón PWR.

## MCU
- ESP32-S3R8: Xtensa LX7 doble núcleo a 240 MHz, 16 MB flash, 8 MB PSRAM octal (`PSRAM=opi`).
- WiFi 2,4 GHz y BLE 5. USB-C al USB-Serial-JTAG nativo (`/dev/cu.usbmodem*`).

## Pantalla
- 1,69" IPS, ST7789V2, 240×280 con esquinas redondeadas, SPI de 4 hilos. La RAM del controlador
  es 240×320: offset de 20 filas, sin offset de columna. Inversión de color activada.
- **Las esquinas del cristal tienen un radio de unos 48 px** (medido con el comando serie
  `corners`: la marca a 8 px de la esquina queda oculta, la de 16 se ve). El firmware lo lleva en
  `LCD_CORNER_RADIUS` y aparta de las esquinas la barra superior, el menú y la página de info.
- Backlight en GPIO 15 con PWM. Ojo: `ledcAttach` reparte canales en orden y dos canales
  consecutivos comparten timer, así que `ledcWriteTone` del zumbador retunea el backlight si se
  deja al azar. El firmware fija canal 0 para el backlight y canal 2 para el zumbador.

| Señal | GPIO |
|---|---|
| LCD DIN (MOSI) | 7 |
| LCD CLK | 6 |
| LCD CS | 5 |
| LCD DC | 4 |
| LCD RST | 8 |
| LCD BL | 15 |

## Táctil y sensores (I2C compartido)
| Dispositivo | Dirección | Pines | Notas |
|---|---|---|---|
| Táctil CST816T (chip id 0xB5) | 0x15 | SDA 11, SCL 10, RST 13, INT 14 | sondeado cada 15 ms con auto-sleep desactivado (reg 0xFE); gestos tap, pulsación larga y swipe |
| IMU QMI8658C | 0x6B | INT 38 | sin usar |
| RTC PCF85063 | 0x51 | INT 39 (41 en la revisión antigua) | sin usar; la hora llega por NTP |

**El táctil no está alineado 1:1 con el panel.** Medido con el comando serie `tcal` (cinco cruces)
en las rotaciones 0 y 2 y promediado: en X es 1:1 con un offset de −3 px; en Y el controlador
estira 1,2×, así que el crudo 0 cae en la fila 29 del panel y el 279 en la 261. El firmware aplica
`panel = raw × ganancia + offset` con las constantes `TOUCH_*` de `boards/s3_touch169.h`. Promediar
dos rotaciones opuestas importa: el dedo toca unos 10 px por debajo y a la derecha del centro de la
cruz, y ese sesgo cambia de signo al girar la placa, así que una sola serie sale desviada ~20 px.
Si otra unidad se comporta distinto, `tcal` da los datos para recalibrar. El offset de filas del
panel es 20 en las cuatro rotaciones: con 0 o 40 en las rotaciones espejo la imagen se corta y
desborda por el lado contrario.

## Alimentación y otros
| Periférico | GPIO | Notas |
|---|---|---|
| Zumbador | 42 (33 en la revisión antigua) | PWM; el firmware lo usa para confirmar acciones |
| Batería ADC | 1 | `VBAT = 3 × VADC`; cargador ETA6098 por USB |
| SYS_EN | 41 (35 antigua) | alto para mantener la alimentación desde batería |
| SYS_OUT | 40 (36 antigua) | botón PWR: alto en reposo, **bajo mientras se pulsa** (medido); 2 s apagan la placa en batería |
| Botón BOOT | 0 | strapping; usable como botón tras el arranque |
| Botón RST | — | reset del chip |
| Header | 2, 3, 17, 18 | más SDA/SCL (11/10), TX/RX (43/44), 3V3, GND |

Para la revisión antigua compila con `-DBOARD_S3_TOUCH169_V1` (ver `boards/s3_touch169.h`).
No tiene LED RGB: la retroalimentación que en la C6 da el WS2812 aquí va por el zumbador.

## Toolchain
- FQBN: `esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M,PSRAM=opi`.
- Arduino_GFX 1.6.4:
  ```cpp
  Arduino_DataBus *bus = new Arduino_ESP32SPI(4 /*DC*/, 5 /*CS*/, 6 /*SCK*/, 7 /*MOSI*/, GFX_NOT_DEFINED, FSPI);
  Arduino_GFX *gfx = new Arduino_ST7789(bus, 8 /*RST*/, 0 /*rot*/, true /*ips*/, 240, 280, 0, 20, 0, 20);
  ledcAttach(15, 5000, 8); ledcWrite(15, 200);  // backlight
  ```
- Offsets de flasheo: bootloader `0x0`, particiones `0x8000`, `boot_app0` `0xE000`, app `0x10000`
  (el S3 arranca el bootloader en `0x0`, igual que el C6).
