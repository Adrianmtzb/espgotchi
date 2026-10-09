# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/).
Este proyecto sigue [Versionado Semántico](https://semver.org/lang/es/).

## [Sin publicar]

### Añadido

- **Batería baja y carga en la S3.** A ≤15 % se activa `lowBattery` (se limpia a ≥20 %, con
  histéresis para que no parpadee en el umbral): el icono de la barra superior parpadea en rojo,
  suenan dos notas descendentes (`TUNE_LOWBAT`) al activarse y cada 10 minutos mientras siga baja,
  y el brillo queda limitado al 60 % del configurado hasta que la carga se recupere. `charging` se
  deduce de que la tensión suba ≥40 mV en 60 s (la placa no expone VBUS) y pinta un rayo junto al
  icono. Los dos campos salen en `/api/info` y en el MCP (`get_info`); la consola gana `bat`
  (mV, %, low, charging). Sin verificar aún en hardware con el pack descargado.
- **Snack en el menú del dispositivo**, entre Feed y Play (ocho iconos; la rejilla 2×4 / 4×2
  ya tenía el hueco).
- **Cola de acciones.** Comer, snack, jugar, acariciar, limpiar, medicina y eclosionar pasan por
  `Pet::request()`: si hay una animación en pantalla la acción se encola (hasta cuatro) y arranca
  cuando termina, en vez de cortar la anterior. Aplica al menú, a la consola y a `POST /api/action`,
  que devuelve `queued` y `position`, y responde `429 busy` con la cola llena. `/api/state` expone
  `busy`, `busyMs` y `queued`; la web y el MCP lo muestran. En la placa, una acción encolada suena
  con el tick y parpadea en ámbar.

- Tres especies nuevas, la **edición Cute**: **Mimi** (`mimi`, gatita blanca con lazo rojo y
  vestido rosa), **Momo** (`momo`, conejita crema con una flor en la oreja) y **Pingo** (`pingo`,
  pingüino con bufanda a rayas). Línea de evolución completa, tres formas adultas y frames dormidos
  generados, como el resto. Disponibles en la web, la landing, el MCP y la consola (`reset mimi`).
- Soporte para una segunda placa, la **Waveshare ESP32-S3-Touch-LCD-1.69** (`make build BOARD=s3`):
  pantalla ST7789V2 de 240×280, táctil CST816 (tap sobre un icono, swipe para recorrer el menú,
  pulsación larga), zumbador en lugar del LED, indicador de batería en pantalla y en `/api/info`,
  y botón PWR para apagar. El pinout vive en `firmware/espgotchi/boards/` y lo elige el target
  del compilador; el layout se calcula desde el tamaño del panel. El instalador web lleva un build
  por chip y el CI compila las dos placas. Verificado en hardware: pantalla, táctil (con
  calibración medida), zumbador, batería y botón PWR.
- Hostname mDNS configurable desde la web, la API (`hostname` en `/api/settings`), el MCP y la
  consola (`host <nombre>`), para que dos placas convivan en la misma red. Por defecto sigue
  siendo `espgotchi.local`.
- Ajuste `nightDim` (web, API) para desactivar la atenuación automática de 22:00 a 07:00.
- Sonido de popó en placas con zumbador: un plop descendente cuando aparece una caca nueva (no
  suena si el pet duerme).
- Sonidos por acción en placas con zumbador: comer, snack, jugar, mimar, limpiar, dormir y
  despertar, medicina, info, tic de menú, rechazo, nacimiento o evolución y muerte. Se generan
  desde el estado del pet, así que suenan igual venga la orden del botón, el táctil, la web o el MCP.
  Comer y snack suenan por mordida: un bocado por cada frame de masticar y un remate al terminar.
- Animación de arranque: el huevo de la especie activa cae y rebota sobre su sombra, el nombre se
  escribe solo y aparece la versión con el identificador de la placa, con su propio sonido en las
  placas con zumbador. El comando serie `boot` la repite.
- La pantalla Info cierra con "Follow for more" y el enlace a adrianmb.dev.
- Landing: sección de hardware con las dos placas dibujadas en el estilo del hero (pantalla con
  el pet de la especie elegida, botones, LED o zumbador, batería) y sus fichas y pines; inglés
  por defecto con el selector recordando la elección; tarjeta social (`og:image`) generada por
  `tools/gen_og.mjs` en el CI; cabecera móvil con los enlaces en una tira desplazable.
- Selector de zona horaria en la web con ciudades comunes agrupadas por región; la opción
  "Custom" sigue aceptando un string POSIX a mano.
- Comandos de consola para poner en marcha una placa nueva: `tp` (registro de toques), `tcal`
  (calibración táctil con cinco cruces), `corners` (medir el radio del bisel) y `gpio <n>`.
- Edición especial de Halloween: dos especies nuevas, **Boo** (`ghost`, fantasma de sábana con
  corona de adulto) y **Jack** (`pumpkin`, calabaza tallada con orejas de murciélago y sombrero de
  bruja), con huevo, cuatro etapas, formas elite/feral/anciano y tema propio. Marcadas con
  `season: "halloween"` en `shared/sprites.json`; la web y la landing las etiquetan.
- Servidor MCP en `mcp/` para que un asistente de IA cuide al pet por la API HTTP: estado con
  consejos, eventos, info de la placa, resumen en texto, acciones de cuidado, renombrar, ajustes
  y `new_egg` con confirmación obligatoria. Documentado en `mcp/README.md`.

### Corregido

- El menú del botón se cerraba al instante cuando la pulsación llegaba después de muestrear el
  reloj en la misma vuelta del loop (desbordamiento sin signo del tiempo de espera). Se notaba
  sobre todo en vertical y con el botón simulado por serie.

## [0.1.0] — 2026-10-07

Primera versión pública.

### Añadido

- Mascota virtual para la **Waveshare ESP32-C6-LCD-1.47** con cuatro especies (Kawaii, Alien,
  Dino, Edgerunner), cada una con huevo, bebé, niño, adolescente y adulto, y tres formas
  adultas según los cuidados (normal, elite, feral) más anciano.
- Simulación con hambre, diversión, energía, limpieza y salud; cacas, enfermedad, sueño y
  muerte. Estado persistente en NVS y recuperación del tiempo apagado con hora NTP.
- Pantalla con habitación temática, modo noche, menú de siete acciones y página de info.
- Control con el único botón libre (BOOT): clic, doble clic, pulsación larga y reinicio.
- LED RGB con humor, feedback de acciones y celebración al evolucionar.
- Web embebida servida por el propio ESP32 con espejo de pantalla, acciones, log y ajustes
  (nombre, brillo, orientación, zona horaria, WiFi).
- Portal cautivo con red abierta `ESPgotchi-XXXX` para la primera configuración; luego
  `espgotchi.local` por mDNS.
- API JSON y consola serie.
- Comidas y snacks variados con animación de tres fases; burbujas al limpiar; frames dormidos
  generados automáticamente a partir de los ojos de cada sprite.
- Instalador web con esp-web-tools, Makefile, CI con GitHub Actions y release automática por
  tag.
- Comandos serie `shot`, `press` y `hold` y `make shot` para capturar la pantalla real y simular
  el botón sin tener la placa delante.

[Sin publicar]: https://github.com/Adrianmtzb/espgotchi/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Adrianmtzb/espgotchi/releases/tag/v0.1.0
