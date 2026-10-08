# AGENTS.md

Contexto para asistentes de IA que trabajen en este repositorio. Lee también
[CONTRIBUTING.md](CONTRIBUTING.md): el modelo de ramas, los idiomas y el estilo aplican igual.

## Qué es esto

Firmware Arduino/C++ para la **Waveshare ESP32-C6-LCD-1.47** (ESP32-C6, RISC-V, 4 MB; pantalla
ST7789 172×320, un botón, LED WS2812) y la **Waveshare ESP32-S3-Touch-LCD-1.69** (ESP32-S3,
16 MB + 8 MB PSRAM; ST7789V2 240×280, táctil CST816, zumbador, batería). Una mascota virtual con
una web servida por el propio chip. Fichas de hardware: [BOARD.md](BOARD.md) y
[BOARD-S3-TOUCH-1.69.md](BOARD-S3-TOUCH-1.69.md). El pinout de cada placa está en
`firmware/espgotchi/boards/` y lo elige `config.h` por el target del compilador; todo lo que
dependa de la placa (LED, zumbador, batería, táctil) pasa por `hw.h` y `touch.h` con guardas
`HAS_*`, nunca por `#ifdef` sueltos en el sketch.

## Reglas del repo

- **Código, comentarios, commits, cadenas de UI y logs en inglés. Documentos en español.**
- **No añadas firmas ni atribuciones de IA** en commits, PRs, código ni docs.
- No añadas binarios a `docs/`: están ignorados a propósito y los genera el CI.
- Si no puedes verificar algo en hardware, dilo en lugar de darlo por bueno.

## Ficheros generados

`firmware/espgotchi/sprites.h` y `firmware/espgotchi/web_assets.h` **los escriben
`tools/gen_sprites.mjs` y `tools/gen_web.mjs`**. Llevan cabecera de aviso y están commiteados
para que el sketch compile sin Node, pero editarlos a mano se pierde en la siguiente
regeneración y rompe `make check`.

Fuentes de verdad: `shared/sprites.json` (sprites, especies, paletas, listas de comidas y
snacks) y `web/index.html` (la web embebida). `make build` regenera y compila.

## Comandos

```bash
make deps      # core esp32:esp32@3.2.0 + GFX Library for Arduino 1.6.4 + ArduinoJson 7
make build     # genera + compila (BOARD=c6 por defecto; BOARD=s3 para la S3)
make build-all # compila las dos placas
make flash     # sube por /dev/cu.usbmodem* (BOARD=s3 para la S3)
make monitor   # serie 115200, escribe 'help'
make check     # manifest + generados al día
make sprites   # hoja PNG con todos los sprites (macOS, usa sips)
make og        # tarjeta social de la landing (_site/og.png); la genera el CI, no se commitea
```

- La compilación tarda 1–3 minutos. Usa modo asíncrono si tu herramienta lo tiene.
- Sin pyserial se puede hablar con la consola así:
  `stty -f /dev/cu.usbmodemXXXX 115200 raw -echo; cat /dev/cu.usbmodemXXXX & printf 'status\n' > /dev/cu.usbmodemXXXX`.
- Si la subida no conecta: mantener BOOT, pulsar RST, soltar BOOT y repetir.

## Trampas ya pagadas

- **El panel necesita offset de columna 34 e INVON.** `Arduino_ST7789(bus, RST, rot, true, 172, 320, 34, 0, 34, 0)`. Sin el offset la imagen sale desplazada; sin inversión los colores salen al revés.
- **El backlight va por MOSFET en GPIO22 con PWM** (`ledcAttach(22, 5000, 8)`). Si "el brillo no funciona", mira antes la zona horaria: de 22:00 a 07:00 el modo noche limita el duty a 80. Hubo un bug exactamente así cuando la TZ por defecto era europea y el usuario estaba en México.
- **El AP usa `WIFI_AP_STA`**, hace falta la interfaz STA para escanear redes sin tumbar el punto de acceso. El escaneo es asíncrono (`scanNetworks(true)`).
- **El SSID del AP sale del eFuse MAC**, no de la MAC de la interfaz: la MAC cambia entre modo AP y STA y el nombre de la red bailaba.
- **El SSID de una red puede ser cualquier cosa, incluso `localhost`.** No asumas que un valor raro en el formulario es autocompletado del navegador.
- **Las fuentes FreeSans están vendorizadas** en `firmware/espgotchi/fonts/` con el include de Adafruit_GFX quitado; Arduino_GFX no las trae.
- **`ledcRead()` devuelve el duty real**: sirve para diagnosticar (`bl` en la consola).
- **Las animaciones avanzan con `animFrame()` una vez por segundo** (el tick de simulación), no por frame de render. Las fases de comer son 0, 1 y 2+. Lo que deba moverse suave (pelota, burbujas) usa `millis()`.
- **Los frames dormidos no se dibujan a mano**: `tools/sleepy.mjs` detecta los ojos por índices de paleta (`species.*.eyes`) y pinta un párpado. Ignora tiras de 1 px de alto y ≥4 de ancho para no cerrar bocas ni dientes. La web replica el algoritmo en `sleepyRows`.
- **Para ver la pantalla sin tener la placa delante**: `make shot` (comando serie `shot`, vuelca el framebuffer en base64 y `tools/screenshot.py` lo guarda en PNG). `press` y `hold` simulan el botón BOOT desde la consola, y `boot` repite la animación de arranque (si mandas `boot` y `shot` en la misma línea de consola, `shot` captura el último frame). Úsalo antes de dar por bueno cualquier cambio de layout.
- **El framebuffer del `Arduino_Canvas` es `uint16_t` little-endian** en memoria; si decodificas los bytes al revés los colores salen con rojo y azul intercambiados y parece un problema de MADCTL/BGR que no existe.
- **Nunca compares `now - marca` sin signo cuando `marca` puede ponerse después de muestrear `now`** en la misma vuelta del loop (botón o CLI corren después). El menú se cerraba al instante por ese desbordamiento; hoy la comprobación usa `(int32_t)(millis() - menuShownMs)`.
- **El gzip de `web_assets.h` no es reproducible entre máquinas**: la zlib de macOS y la del runner Linux generan bytes distintos para el mismo contenido. Por eso `gen_web.mjs --check` descomprime lo commiteado y lo compara con las fuentes, en vez de comparar el header byte a byte. No vuelvas a la comparación literal.
- **Las tramas de la web se sirven gzip desde PROGMEM** con `Content-Encoding: gzip`. Si cambias `web/index.html` sin regenerar, el navegador seguirá viendo la versión vieja.
- **Los offsets del instalador están verificados** contra el `merged.bin` de cada placa: bootloader `0x0`, particiones `0x8000`, boot_app0 `0xE000`, app `0x10000`. C6 y S3 arrancan el bootloader en `0x0`, no en `0x1000` como el ESP32 clásico. El manifest lleva un build por chip con partes `<parte>-<placa>.bin`; `scripts/check-manifest.py` los vigila.
- **El layout de la UI se deriva de W/H en `Ui::begin`** (`roomX/roomW/panelX/statRowH`...). No vuelvas a meter números de 320×172 a mano: la S3 es 280×240 en horizontal y 240×280 en vertical, y la geometría del menú la comparte `menuHit()` con el táctil.
- **El panel de la S3 (ST7789V2 240×280) usa offset de 20 filas y 0 columnas**, y el cristal tiene esquinas de radio ~48 px (`LCD_CORNER_RADIUS`). `Ui::edgeInset(dist)` dice cuántos píxeles se come la esquina a esa distancia del borde; úsalo en vez de márgenes a ojo. El comando serie `corners` mide el radio en una placa nueva.
- **El táctil de la S3 no coincide 1:1 con el panel**: en Y llega estirado 1.2× (el crudo 0 cae en la fila 29 y el 279 en la 261). `touch.cpp` aplica `TOUCH_*_GAIN/OFFSET` del header de la placa, medidos con el comando serie `tcal` (cinco cruces, imprime canvas vs crudo). Mide en dos rotaciones opuestas y promedia: el dedo cae ~10 px abajo y a la derecha del centro de la cruz, y ese sesgo cambia de signo al girar la placa. No "arregles" un toque desviado tocando la rotación ni el offset de filas del panel (0 y 40 ya se probaron, los dos rompen la imagen): primero `tp` y `tcal`.
- **Dos canales LEDC consecutivos comparten timer** y `ledcWriteTone` retunea ese timer: con `ledcAttach` automático el zumbador dejó el backlight a 440 Hz. Backlight en canal 0, zumbador en canal 2 (`ledcAttachChannel`).
- **SYS_OUT (botón PWR de la S3) está alto en reposo y bajo al pulsar.** Con la polaridad al revés la placa "se apagaba" cada 2 s, que en USB solo se nota como un bip periódico.

## Seguridad

Ver [SECURITY.md](SECURITY.md). La API no devuelve nunca la contraseña del WiFi, los SSID
escaneados se insertan en el DOM con `textContent`, y todo índice o tipo que llega por HTTP se
valida contra el rango real.
