# Seguridad

## Qué es y qué no es

ESPgotchi es un juguete que corre en una placa de desarrollo y sirve una web en tu red local.
No está pensado para exponerse a Internet: no tiene autenticación, HTTPS ni control de acceso.
Cualquiera que esté en la misma red puede alimentar al pet, renombrarlo o cambiar el WiFi.

Modelo de amenaza razonable:

- **Red doméstica de confianza.** Quien está en tu WiFi puede hablar con la placa. Si eso te
  preocupa, ponla en una red de invitados o de IoT.
- **Portal cautivo abierto.** En modo configuración la red `ESPgotchi-XXXX` no tiene contraseña
  (decisión de diseño, para que el portal funcione sin fricción en cualquier móvil). Dura hasta
  que guardas tus credenciales. Mientras tanto, cualquiera cerca puede conectarse y darle una
  red; no puede leer nada tuyo porque todavía no hay nada guardado.
- **Credenciales WiFi en NVS.** Se guardan en la flash del ESP32 sin cifrar, como en casi
  cualquier proyecto Arduino. Quien tenga acceso físico al USB puede leerlas. No reutilices
  una contraseña importante.
- **La API no devuelve nunca la contraseña del WiFi**, solo el SSID.

## Reportar una vulnerabilidad

Si encuentras algo que vaya más allá de lo anterior (ejecución de código, cuelgue remoto,
fuga de credenciales por la API), repórtalo en privado:

- [Aviso de seguridad privado en GitHub](https://github.com/Adrianmtzb/espgotchi/security/advisories/new)

Incluye la versión del firmware (`/api/info` o `status` por serie) y cómo reproducirlo.
Respondo en cuanto puedo; no hay SLA, es un proyecto personal.

## Versiones soportadas

Solo la última release. No hay ramas de mantenimiento.
