# ESPgotchi

[![CI](https://github.com/Adrianmtzb/espgotchi/actions/workflows/ci.yml/badge.svg)](https://github.com/Adrianmtzb/espgotchi/actions/workflows/ci.yml)
[![Licencia: MIT](https://img.shields.io/badge/licencia-MIT-blue.svg)](LICENSE)

Mascota virtual libre para la **Waveshare ESP32-C6-LCD-1.47** y la **Waveshare
ESP32-S3-Touch-LCD-1.69**. Diez especies en pixel-art, cinco etapas de evolución y un carácter que
depende de los cuidados. Se controla desde el botón BOOT, desde la pantalla táctil en la S3, o
desde una web que sirve el propio ESP32 por WiFi. No necesita app ni servidor.

**Instalación desde el navegador:** https://adrianmtzb.github.io/espgotchi/

Usa Web Serial, así que el firmware se escribe directo al dispositivo desde Chrome o Edge de
escritorio; nada pasa por un servidor. La misma página tiene una demo del pet corriendo con los
sprites reales. Si prefieres compilarlo, mira [Compilar y flashear](#compilar-y-flashear).

Proyecto independiente. No está afiliado a Waveshare ni a Bandai; las marcas se mencionan solo
para identificar el hardware compatible.

---

## Hardware

Dos placas soportadas. El sketch elige el pinout por el target del compilador
(`firmware/espgotchi/boards/`), así que la placa es solo el `BOARD=` del Makefile.

| | ESP32-C6-LCD-1.47 (`BOARD=c6`) | ESP32-S3-Touch-LCD-1.69 (`BOARD=s3`) |
|---|---|---|
| Pantalla | ST7789 172×320, horizontal por defecto | ST7789V2 240×280, esquinas redondeadas |
| Entrada | botón BOOT | botón BOOT + táctil CST816 (tap, swipe, pulsación larga) |
| Feedback | LED WS2812 según el humor | zumbador (tic al aceptar, zumbido grave al rechazar, arpegio al evolucionar, plop al hacer popó, ronquidos al dormir, estornudos si enferma y un saludo al conectar al WiFi) |
| Extras | — | batería con indicador en pantalla y API, botón PWR para apagar |
| Ficha | [BOARD.md](BOARD.md) | [BOARD-S3-TOUCH-1.69.md](BOARD-S3-TOUCH-1.69.md) |

El layout se calcula desde el tamaño del panel, así que las cuatro orientaciones funcionan en
ambas. La S3 está portada a partir de la documentación oficial; mira la ficha para saber qué
queda por verificar en hardware.

### ESP32-C6-LCD-1.47

Datos tomados de la [wiki oficial](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47), el
[esquemático](https://files.waveshare.com/wiki/ESP32-C6-LCD-1.47/ESP32-C6-LCD-1.47_schemetics.pdf)
y el [demo oficial](https://files.waveshare.com/wiki/ESP32-C6-LCD-1.47/ESP32-C6-LCD-1.47-Demo.zip).

| Función | GPIO | Notas |
|---|---|---|
| LCD MOSI / SCLK | 6 / 7 | ST7789, 172×320, SPI a 80 MHz |
| LCD CS / DC / RST | 14 / 15 / 21 | |
| LCD backlight | 22 | MOSFET, activo alto, PWM |
| LED RGB | 8 | WS2812B |
| microSD CS / MISO | 4 / 5 | comparte MOSI/SCLK con la LCD (sin usar) |
| Botón BOOT | 9 | activo bajo, pull-up de 10K en placa |
| Botón RST | — | reset del chip, no programable |
| USB-C | — | USB nativo del C6 (aparece como `/dev/cu.usbmodem*`) |

El panel necesita offset de columna 34 e inversión de color activada. No hay táctil.

## Qué hace

- **Diez especies** con estilo propio, cada una con huevo, bebé, adolescente y adulto en
  pixel-art de 24×24 y tema de color para pantalla y web: **Kawaii** (gatito pastel), **Alien**
  (verde con antenas), **Dino** (con crestas naranjas), **Edgerunner** (gato cyberpunk con
  visor neón) y la **edición especial de Halloween**: **Boo** (fantasma de sábana que de adulto
  gana corona) y **Jack** (calabaza tallada que acaba con sombrero de bruja), y la **edición
  Cute**: **Mimi** (gatita con lazo), **Momo** (conejita con flor) y **Pingo** (pingüino con
  bufanda), y la **edición Fantasy**: **Nova** (unicornio blanca con crin rosa y cuerno dorado).
  Al crear un huevo se elige la especie o se deja al azar.
- **Pantalla**: habitación con el color de la especie, mascota animada con sombra, panel de
  estadísticas (comida, diversión, energía, limpieza, salud), cacas, Zz al dormir, alerta cuando
  necesita algo, calavera si muere. Modo noche (22:00–07:00 o luces apagadas) con cielo
  estrellado y backlight reducido. Tipografías FreeSans para nombre y menú.
- **Ciclo de vida**: huevo (1 min) → bebé (30 min) → niño (2 h) → adolescente (8 h) → adulto
  (48 h) → anciano. Al llegar a adulto la **forma** depende de los cuidados: *elite* (≤1 error de
  cuidado, salud ≥80 y ánimo ≥60, paleta dorada), *feral* (≥5 errores o salud <40, paleta oscura
  con ojos rojos) o normal; el anciano tiene paleta gris. Las estadísticas caen con el tiempo; la
  suciedad y el hambre producen enfermedad; la salud a cero mata al pet.
- **Orientación** configurable desde la web o la API: horizontal, horizontal invertida, vertical
  o vertical invertida (la placa reinicia para aplicar el cambio y el layout se adapta).
- **Persistencia**: estado en NVS cada minuto y en cada acción. Si hay hora NTP, al arrancar
  simula el tiempo que estuvo apagado (máximo 8 h).
- **LED RGB**: humor y feedback. Verde bien, doble parpadeo ámbar cada 2 s si necesita
  atención, rojo intermitente enfermo, destello blanco al aplicar una acción (desde botón,
  web o CLI), destello rojo si se rechaza, arcoíris al eclosionar o evolucionar, azul tenue
  dormido, blanco huevo, violeta muerto.
- **Web embebida** en `http://espgotchi.local/` (o la IP): espejo de la pantalla, barras,
  acciones, log de eventos, renombrar, brillo, zona horaria y cambio de WiFi.
- **API JSON** para integrar lo que quieras (ver abajo).
- **CLI por serie** a 115200 baudios: `help`, `status`, `wifi <ssid> <pass>`, `forget`, `name`,
  `tz`, `feed`, `snack`, `play`, `pet`, `clean`, `sleep`, `med`, `bl [0-255]` (diagnóstico del
  backlight), `shot` (vuelca la pantalla), `press` y `hold` (simulan el botón), `hatch`,
  `reset [especie]`, `reboot`.

### Botón BOOT

| Gesto | Efecto |
|---|---|
| Pulsación corta | Abre el menú / avanza al siguiente icono (Feed, Snack, Play, Pet, Clean, Lights, Meds, Info). Con huevo: eclosiona. |
| Doble clic (menú cerrado) | Acaricia al pet (Pet) sin pasar por el menú. |
| Pulsación larga (0,6 s) | Ejecuta el icono seleccionado. Sin menú abierto: muestra la página de info (IP, WiFi, stats). |
| Mantener 6 s | Reinicia con un huevo nuevo (también si ha muerto). |

## Compilar y flashear

Requisitos: [arduino-cli](https://arduino.github.io/arduino-cli/), Node ≥ 18 (para regenerar los
assets embebidos) y `make`. Las versiones del core y las librerías están fijadas en el Makefile:
core `esp32:esp32` 3.2.0, `GFX Library for Arduino` 1.6.4 y `ArduinoJson` 7.

```bash
make deps      # una vez: instala core y librerías
make build     # regenera sprites.h y web_assets.h y compila (BOARD=c6 por defecto, BOARD=s3)
make build-all # compila las dos placas
make flash     # compila y sube (detecta /dev/cu.usbmodem*; o PORT=...; BOARD=s3 para la S3)
make monitor   # consola serie a 115200; escribe 'help'
make check     # valida manifest, versión, ficheros generados y coherencia firmware/web/landing/MCP (lo mismo que el CI)
make sprites   # hoja PNG con todos los sprites (macOS)
make shot      # captura la pantalla real de la placa en PNG (sin pyserial)
make           # lista todos los atajos
```

FQBN de la C6: `esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M`. De la S3:
`esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB,FlashSize=16M,PSRAM=opi`.
Si la subida no conecta, mantén BOOT, pulsa RST, suelta BOOT y repite. `scripts/build.sh` y
`scripts/flash.sh` siguen funcionando si no quieres `make`.

### Instalador web y releases

`docs/` contiene la landing (`index.html`) y el `manifest.json` de
[esp-web-tools](https://esphome.github.io/esp-web-tools/). Los binarios no viven en el repo: al
crear un tag `vX.Y.Z` el CI compila, publica la release en GitHub y despliega la página en GitHub
Pages con los `.bin` recién compilados. `make site` arma lo mismo en `_site/` y `make serve` lo
sirve en `localhost:8000` para probar el instalador antes de publicar. `make bump VERSION=X.Y.Z`
sube la versión en `config.h` y en el manifest a la vez.

El manifest lleva un build por chip (`ESP32-C6` y `ESP32-S3`) y esp-web-tools elige el que toca
según la placa conectada. Los offsets están verificados contra el `merged.bin` de cada una:
bootloader `0x0`, particiones `0x8000`, `boot_app0` `0xE000`, aplicación `0x10000`.
`scripts/check-manifest.py` los vigila.

## Primera conexión WiFi

Sin credenciales guardadas (o si no logra conectar en 30 s) la placa levanta un punto de acceso
con **portal cautivo**:

1. Conéctate a la red abierta `ESPgotchi-XXXX` (sin contraseña; el sufijo sale de la MAC y no
   cambia).
2. El sistema abre solo la página de configuración (si no, ve a `http://192.168.4.1`). Elige tu
   red en la lista de redes detectadas, pon la contraseña y guarda. La placa reinicia.
3. En tu red abre `http://espgotchi.local/` (o la IP que muestra la página Info del botón).

Si las credenciales guardadas fallan, el portal lo indica y permite corregirlas. Alternativas por
serie: `wifi MiRed MiClave` para configurar, `forget` para borrar y volver al modo setup. La zona horaria por defecto es
America/Mexico_City (`CST6`); cámbiala desde el selector de la web (ciudades comunes, o un string
POSIX a mano) o con `tz <posix>` (por ejemplo `CET-1CEST,M3.5.0,M10.5.0/3` para España). De 22:00 a 07:00 la pantalla entra en modo noche y el
brillo queda limitado a 80.

## API HTTP

| Método y ruta | Descripción |
|---|---|
| `GET /` | Web embebida (en modo AP: formulario de WiFi) |
| `GET /sprites.json` | Sprites compartidos con la web |
| `GET /api/state` | Estado del pet (`stage`, `form`, `nextEvolutionSec`, stats…) y del dispositivo |
| `GET /api/events` | Últimos 16 eventos, el más reciente primero |
| `GET /api/info` | Placa, firmware, IP, RSSI, heap, TZ, brillo |
| `POST /api/action` | `{"type":"feed"|"snack"|"play"|"pet"|"clean"|"sleep"|"medicine"|"hatch"|"reset"}`; con `reset`, opcional `"species":"kawaii"|"alien"|"dino"|"edge"|"ghost"|"pumpkin"|"mimi"|"momo"|"pingo"|"unicorn"` |
| `POST /api/name` | `{"name":"Pixel"}` (máx. 15 caracteres) |
| `POST /api/settings` | `{"tz":"...", "brightness": 5..255, "orientation":"landscape"|"landscape-flipped"|"portrait"|"portrait-flipped", "hostname":"espgotchi", "nightDim": true}` (orientación y hostname reinician la placa; `nightDim` activa o quita la atenuación nocturna) |
| `POST /api/wifi` | `{"ssid":"...","pass":"..."}` o formulario; guarda y reinicia |

Ejemplo:

```bash
curl -s http://espgotchi.local/api/state | jq .pet
curl -s -X POST http://espgotchi.local/api/action -H 'content-type: application/json' -d '{"type":"feed"}'
```

## Estructura

```
firmware/espgotchi/    sketch Arduino (config.h, pet.*, ui.*, net.*, espgotchi.ino)
                        sprites.h y web_assets.h son generados, no editar a mano
                        fonts/ son las FreeSans de Adafruit GFX (licencia BSD) copiadas
web/index.html          web embebida (vanilla JS, sin dependencias)
shared/sprites.json     sprites en ASCII (pets 24×24, iconos 16×16), temas por especie, paletas
                        de las formas adultas y listas de comidas y snacks; fuente única para
                        firmware, web y landing
tools/                  gen_sprites.mjs, gen_web.mjs, sleepy.mjs (frames dormidos) y
                        preview_sprites.py
docs/                   landing bilingüe e instalador web (manifest.json); los .bin los pone el CI
scripts/                build.sh, flash.sh, monitor.sh y check-manifest.py
tools/screenshot.py     captura la pantalla de la placa por serie (comando `shot`)
mcp/                    servidor MCP para que una IA cuide al pet (ver mcp/README.md)
Makefile                atajos de desarrollo; el CI ejecuta exactamente estos targets
firmware/espgotchi/boards/  un header de pinout por placa, elegido por el target del compilador
BOARD.md                ficha de hardware de la ESP32-C6-LCD-1.47
BOARD-S3-TOUCH-1.69.md  ficha de hardware de la ESP32-S3-Touch-LCD-1.69
```

Para editar sprites o la web, cambia la fuente y ejecuta `make build`. Un sprite puede definirse
como parche de otro (`{"base": "...", "patch": {"11": "..."}}`) para los frames de parpadeo. Los
frames dormidos no se dibujan: `tools/sleepy.mjs` cierra los ojos del frame despierto a partir de
los índices de paleta declarados en `species.*.eyes`. Para desarrollo de la web sin reflashear,
abre `web/index.html?host=<IP-de-la-placa>` en el navegador.

## Cuidarlo con una IA (MCP)

En `mcp/` hay un servidor [MCP](https://modelcontextprotocol.io) que expone el cuidado del pet
como herramientas: ver estado y consejos, dar de comer, jugar, acariciar, limpiar, medicar,
apagar la luz, etc. Conéctalo a Claude Code, Claude Desktop o cualquier cliente MCP y pídele
"¿cómo está Pixel?". Instalación y configuración en [mcp/README.md](mcp/README.md).

## Contribuir

Se aceptan PRs. Lee [CONTRIBUTING.md](CONTRIBUTING.md) para el modelo de ramas, los idiomas y
cómo probar los cambios. Si usas un asistente de IA, [AGENTS.md](AGENTS.md) tiene el contexto y
las trampas conocidas del proyecto. Otros documentos: [CHANGELOG.md](CHANGELOG.md) y
[SECURITY.md](SECURITY.md).

## Licencia

[MIT](LICENSE). Las fuentes FreeSans embebidas son de Adafruit GFX (BSD).
