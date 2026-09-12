# VIS-2 — Identidad persistente y editor local

2026-09-12. **Implementado y verificado en software; sin nueva carga a COM6.**
[Contrato HTTP/NVS](../display-identity-api.md),
[plan visual](../PLANS/2026-09-12_display-visual-identity.md).

La configuración de nombre/teléfono/canal ya se guarda fuera de `ConfigRecord`,
en dos registros NVS de 84 bytes con CRC y lectura posterior. No-op no escribe;
generación esperada impide sobrescribir una edición concurrente. El getter
ofrece datos canónicos para el adaptador de pantalla de VIS-3. Ningún perfil
incluye nombre/teléfono reales por defecto.

`GET/POST /api/identity` y la sección «Placa de mi perro» de `/config` incluyen
normalización NFC en navegador, validación independiente en firmware, vista
previa textual del contacto/destino y guardado/borrado propios. El borrador
participa en el aviso de salida sin contaminar el estado pendiente LED/GPS.
Los errores/conflictos conservan lo escrito; recargar es una acción explícita.
Classic informa `supported:false` y oculta los controles; mantiene exclusión
de `display/` y no usa LVGL ni el store de identidad.

## Verificación

| Ensayo | Resultado y límite |
| --- | --- |
| CTest nativo | 10/10 OK: siete anteriores + store real + handlers reales Display/Classic |
| Store sobre Preferences en memoria | Reinicio, banco alterno, no-op, rechazo/truncamiento/corrupción, CRC con datos inválidos, clear, conflicto y rollover |
| HTTP sobre WebServer/guard grabados | JSON/campos/tipos, NUL embebido, límite 512 B, guard invocado, respuestas/canal/payload, error de almacenamiento y Classic |
| Suite Python | 147/147 OK |
| Web unit | 7/7 OK; incluye NFC, alfabeto/límites y clear del editor real |
| Portal móvil existente | 18/18 OK con capability Classic en fixtures; sin comparación visual de baselines Linux |
| Playwright CLI, navegador Chromium 428×926 | NFC antes de enviar, teléfono canónico, cabecera de escritura, guardar/recargar, errores 500/409 sin perder borrador, llamada, borrar y ocultar en Classic |

Las pruebas HTTP compilan los handlers actuales extraídos de `portal_http.cpp`
y ArduinoJson del entorno Display; no reimplementan el parser en Python.
El transporte, la autorización final y NVS son dobles de prueba: no acreditan
radio, corte físico de alimentación ni tiempos de escritura del ESP32.

La prueba de error de lectura posterior distingue un resultado indeterminado:
RAM retiene la versión verificada, pero tras reiniciar puede cargarse el nuevo
registro completo si la escritura sí ocurrió. No se devuelve éxito ante ese
error. Semántica y recuperación están documentadas en el contrato.

Capturas de navegador con contactos sintéticos:
[guardado](../assets/display-vis2/portal-saved.png) /
[conflicto y borrador conservado](../assets/display-vis2/portal-conflict.png).
La vista previa del portal muestra datos/enlace, no intenta duplicar LVGL.

## Recursos y siguiente paso

Página `/config`: 23.880 B gzip; presupuesto por ruta ampliado de 23 a 24 KiB
para el editor, tras reducir copia redundante. Total de cuatro páginas:
47.414 B, dentro de 55 KiB. Sin biblioteca web nueva. Assets regenerados con
Node 24.18.0 y comprobados mediante build/round-trip/hashes/smoke.

Matriz de siete targets y tamaños finales: [manifest](../assets/display-vis2/manifest.json).
Classic suma el endpoint de capability negativa y el HTML común; no código
gráfico. Display enlaza el store/API, pero **IdentityView y sus fuentes aún
no están activados en el port**: falta medir su coste conjunto y rendimiento
en VIS-3. Los presupuestos de UI no quedan acreditados por el tamaño actual.

**Siguiente VIS-3:** copiar identidad desde el store, arrancar Placa solo si
hay datos válidos e integrar Placa→Actividad→Wi-Fi→Estado; conservar el primer
clic de despertar. Luego cargar por USB, configurar el contacto solicitado
desde la API/portal local y verificar QR óptico, navegación, reinicio y memoria.
Sin GPS/tiras conectados, V2/V3 y uso portátil continúan pendientes.
