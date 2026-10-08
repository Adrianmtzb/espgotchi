# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/).
Este proyecto sigue [Versionado Semántico](https://semver.org/lang/es/).

## [Sin publicar]

### Añadido

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
