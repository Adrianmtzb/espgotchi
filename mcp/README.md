# ESPgotchi MCP

Servidor [MCP](https://modelcontextprotocol.io) (Model Context Protocol) que convierte el
cuidado del pet en herramientas para un asistente de IA. Conectado a Claude Code, Claude
Desktop o cualquier otro cliente MCP, la IA puede ver cómo está el pet, recibir consejos de
cuidado y actuar: darle de comer, jugar, acariciarlo, limpiarlo, medicarlo o apagarle la luz.

Habla con la placa por su API HTTP (la misma que usa la web embebida), así que no hace falta
reflashear nada: basta con que la placa esté en la misma red.

## Requisitos

- Node.js 18 o superior.
- Una ESPgotchi encendida y conectada al WiFi (sirve `espgotchi.local` o su IP).

## Instalación

```bash
cd mcp
npm install
```

El servidor lee la dirección de la placa de la variable `ESPGOTCHI_HOST` (por defecto
`espgotchi.local`). Acepta un host, `host:puerto` o una URL `http://...` completa. También se
puede pasar `--host 192.168.1.50` como argumento, que prevalece sobre la variable.

### Claude Code

```bash
claude mcp add espgotchi -e ESPGOTCHI_HOST=192.168.50.117 -- node /ruta/absoluta/mcp/server.mjs
```

### Claude Desktop

En `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "espgotchi": {
      "command": "node",
      "args": ["/ruta/absoluta/mcp/server.mjs"],
      "env": { "ESPGOTCHI_HOST": "192.168.50.117" }
    }
  }
}
```

## Herramientas

| Herramienta | Qué hace |
|---|---|
| `get_state` | Estado completo del pet más una lista `advice` con lo que conviene hacer ahora |
| `get_events` | Últimos 16 eventos (comidas, evoluciones, cacas, enfermedad…), el más reciente primero |
| `get_info` | Placa, firmware, heap libre, IP, hora local, modo noche, brillo y zona horaria |
| `screen_text` | Resumen en texto de la pantalla, con barras `▓▓▓░░` de cada stat |
| `feed` | Comida completa; rechazada si duerme, está lleno, es huevo o ha muerto |
| `snack` | Golosina: anima pero engorda, no abusar |
| `play` | Jugar a la pelota: sube la felicidad, gasta energía |
| `pet` | Acariciar: felicidad sin efectos secundarios (funciona incluso dormido) |
| `clean` | Limpiar al pet y las cacas de la pantalla |
| `medicine` | Medicina, solo útil si está enfermo |
| `hatch` | Eclosionar el huevo cuando esté listo |
| `toggle_lights` | Apagar o encender la luz (para que duerma o se despierte) |
| `new_egg` | **Borra el pet** y empieza con un huevo nuevo; exige `confirm: true` y admite `species` |
| `rename` | Cambiar el nombre (hasta 15 caracteres) |
| `set_settings` | Zona horaria (`tz`, cadena POSIX) y brillo (`brightness`, 5..255) |

Cada acción devuelve `applied` (si la placa la aceptó), el motivo si no, el nuevo estado y los
consejos actualizados.

Además expone los recursos `espgotchi://state` y `espgotchi://events` (JSON) y el prompt
`caretaker`, con las reglas de un buen cuidador: revisar de vez en cuando, actuar según `advice`,
no abusar de los snacks, respetar el sueño y no usar `new_egg` salvo que el pet haya muerto y la
persona esté de acuerdo.

## Ejemplo

> **Tú:** ¿cómo está Pixel?
>
> **IA:** *(llama a `get_state`)* Pixel es un kawaii en etapa niño. Tiene hambre (32/100) y hay
> una caca en pantalla; lo demás va bien. ¿Le doy de comer y lo limpio?
>
> **Tú:** sí
>
> **IA:** *(llama a `feed` y `clean`)* Hecho: comió un ramen y la pantalla está limpia. Felicidad
> 88, hambre 92. Vuelvo a mirar en media hora.

## Avisos

- La API de la placa **no tiene autenticación**: cualquiera en tu red local puede controlar al
  pet. No la expongas a Internet.
- `new_egg` **borra al pet actual** sin vuelta atrás. El servidor exige `confirm: true`, pero la
  última palabra la tiene quien habla con la IA.
- Los timeouts son de unos 5 segundos; si la placa no responde, el error indica la dirección
  usada para que puedas corregir `ESPGOTCHI_HOST`.
