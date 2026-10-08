# AGENTS.md

Contexto para asistentes de IA que trabajen en este repositorio. Lee también
[CONTRIBUTING.md](CONTRIBUTING.md): el modelo de ramas, los idiomas y el estilo aplican igual.

## Qué es esto

Firmware Arduino/C++ para la **Waveshare ESP32-C6-LCD-1.47** (ESP32-C6, RISC-V, 4 MB). Una
mascota virtual con pantalla ST7789 de 172×320, un botón, un LED WS2812 y una web servida por
el propio chip. La ficha de hardware está en [BOARD.md](BOARD.md).

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
make build     # genera + compila (FQBN esp32c6, CDCOnBoot=cdc, no_ota, 4M)
make flash     # sube por /dev/cu.usbmodem*
make monitor   # serie 115200, escribe 'help'
make check     # manifest + generados al día
make sprites   # hoja PNG con todos los sprites (macOS, usa sips)
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
- **Para ver la pantalla sin tener la placa delante**: `make shot` (comando serie `shot`, vuelca el framebuffer en base64 y `tools/screenshot.py` lo guarda en PNG). `press` y `hold` simulan el botón BOOT desde la consola. Úsalo antes de dar por bueno cualquier cambio de layout.
- **El framebuffer del `Arduino_Canvas` es `uint16_t` little-endian** en memoria; si decodificas los bytes al revés los colores salen con rojo y azul intercambiados y parece un problema de MADCTL/BGR que no existe.
- **Nunca compares `now - marca` sin signo cuando `marca` puede ponerse después de muestrear `now`** en la misma vuelta del loop (botón o CLI corren después). El menú se cerraba al instante por ese desbordamiento; hoy la comprobación usa `(int32_t)(millis() - menuShownMs)`.
- **El gzip de `web_assets.h` no es reproducible entre máquinas**: la zlib de macOS y la del runner Linux generan bytes distintos para el mismo contenido. Por eso `gen_web.mjs --check` descomprime lo commiteado y lo compara con las fuentes, en vez de comparar el header byte a byte. No vuelvas a la comparación literal.
- **Las tramas de la web se sirven gzip desde PROGMEM** con `Content-Encoding: gzip`. Si cambias `web/index.html` sin regenerar, el navegador seguirá viendo la versión vieja.
- **Los offsets del instalador están verificados** contra el `merged.bin` del C6: bootloader `0x0`, particiones `0x8000`, boot_app0 `0xE000`, app `0x10000`. El C6 arranca el bootloader en `0x0`, no en `0x1000` como el ESP32 clásico. `scripts/check-manifest.py` los vigila.

## Seguridad

Ver [SECURITY.md](SECURITY.md). La API no devuelve nunca la contraseña del WiFi, los SSID
escaneados se insertan en el DOM con `textContent`, y todo índice o tipo que llega por HTTP se
valida contra el rango real.
