# Cómo contribuir

Gracias por pasarte. Es un proyecto pequeño y el proceso es ligero a propósito.

---

## Modelo de ramas

**Trunk-based**: una sola rama larga, `main`, siempre en estado publicable.

```
main ────●────●────●────●──── (siempre desplegable, protegida)
          \        /
           ●──●──●  feat/algo   (vida corta, se fusiona con squash)
```

- `main` está protegida. No se hace push directo.
- Cada cambio va en una rama corta que sale de `main` y vuelve por PR.
- Las PRs se fusionan con **squash**, para que `main` quede con un commit legible por cambio.
- Las versiones se marcan con **tags** `vX.Y.Z`, no con ramas. `make bump VERSION=X.Y.Z`
  actualiza `config.h` y `docs/manifest.json` a la vez.

Prefijos de rama: `feat/`, `fix/`, `docs/`, `refactor/`, `chore/`.

---

## Idiomas

- **Código, comentarios, mensajes de commit, cadenas de la UI y logs: en inglés.**
- **Documentación (README, guías, CHANGELOG): en español.** La landing de `docs/` es
  bilingüe español/inglés con el marcado duplicado y CSS que oculta la lengua que no toca.

---

## Antes de mandar la PR

El CI compila cada PR y comprueba que los ficheros generados están al día. Lo que **no** puede
comprobar es lo importante de este proyecto:

1. **Que funcione en hardware.** El temporizado del botón, el backlight, el LED y el portal
   cautivo no se verifican sin la placa delante. Cuenta en la PR qué probaste.
2. **Si tocaste la red**, prueba los dos modos: arranque sin credenciales (portal) y con
   credenciales válidas (STA + mDNS).
3. **Si tocaste `web/index.html`**, ábrela en un móvil de verdad. Es la pantalla principal
   de uso y hay diferencias reales con el escritorio.
4. **Si tocaste sprites**, edita `shared/sprites.json` y nunca `sprites.h`. Mira el resultado
   con `make sprites` antes de flashear.

```bash
make deps     # una vez: core ESP32 3.2.0 y librerías fijadas
make build    # regenera assets y compila
make flash    # compila y sube (detecta el puerto usbmodem)
make monitor  # consola serie; escribe 'help'
make check    # lo mismo que valida el CI
```

### Ficheros generados

`firmware/espgotchi/sprites.h` y `web_assets.h` los escriben `tools/gen_sprites.mjs` y
`tools/gen_web.mjs` a partir de `shared/sprites.json` y `web/index.html`. Van commiteados
para que el sketch compile con arduino-cli a secas, pero **no se editan a mano**: `make check`
falla si no cuadran con sus fuentes.

### Los binarios del instalador

No están en el repositorio. `docs/` guarda la página y el `manifest.json`; los `.bin` los
compila y publica el CI al crear un tag. Así nunca hay conflictos binarios entre PRs ni riesgo
de que la página instale una versión distinta de la que está en el código.

---

## Cómo añadir una especie

1. En `shared/sprites.json`, añade la entrada en `species` (nombre, acento, fondos de día y
   de noche, índices de paleta de los ojos para generar el frame dormido).
2. Dibuja `<clave>_egg` (16×16) y `<clave>_{baby,child,teen,adult}_{0,1}` (24×24, dos frames).
   Las formas `adult_elite`, `adult_feral` y `adult_elder` suelen ser el adulto con otra paleta:
   usa `{"base": "...", "patch": {...}, "palette": [...]}`.
3. `make sprites` para revisar la hoja, `make build` y a probarla. La web y el selector de
   especie se actualizan solos porque leen el mismo JSON.

---

## Estilo

- C++ al estilo del código existente: sin excepciones ni `std::string`, nada de `delay()` en
  el loop, `millis()` para todo lo temporal.
- Comentarios solo donde explican un *por qué* que el código no cuenta.
- Sin firmas ni atribuciones de herramientas o asistentes en commits, PRs, código ni docs.
