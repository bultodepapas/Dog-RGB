# VIS-4 — Nombre y componentes visuales compartidos

2026-09-12. Implementación estática de Actividad, Wi-Fi y Estado. Conserva la
Placa aceptada en VIS-3. [Guía](../../Platformio/Dog-RGB/docs/display-vis4.md),
[capturas, fuentes y binarios](../assets/display-vis4/manifest.json).

## Resultado

`PageHeader` muestra el nombre del perfil, el índice y `GPS DEMO` independiente.
`ui_theme` comparte negro, colores, etiquetas/divisores y setters sin
invalidación redundante. Nombre Montserrat 600/14 px/4 bpp, con cobertura latina
y `...` medido cuando no cabe; corte UTF-8 íntegro. La marca demo reserva su
espacio; no se confunde con el nombre ni implica Wi-Fi/LED simulados.

El port propaga también renombrados con teléfono/canal iguales. Perfil ausente
usa RGB DOG. Nombre completo y teléfono permanecen en Placa; no se escriben
abreviaciones a NVS. Conserva métricas, validez, SSID/IP y política LED efectiva.
Sin animación, tareas, cambios de radio, persistencia o bibliotecas.

Capturas compartidas: [Actividad](../assets/display-vis4/owner-activity.png),
[Wi-Fi/demo](../assets/display-vis4/owner-wifi-demo.png),
[Estado con tilde](../assets/display-vis4/owner-accent.png),
[nombre extremo](../assets/display-vis4/owner-wide-demo.png).
Son renders reales de LVGL en PC, no fotografías del panel.

## Verificación y coste

- **12/12 CTest** y **152/152 Python**, esta última en 34,483 s.
- Renderer ampliado de 14 a **21 PNG**, más **11 PNG del port**. Siete casos
  nuevos: nombre corto, demo, tilde, ñ, largo, ancho extremo e inválido.
- Glifos latinos, 24 caracteres extremos, ancho/separación, ausencia de flush
  en 100 updates iguales y memoria estable en 30 ciclos de nombre/demo/página.
- **Siete renders de Placa idénticos** a VIS-3; QR exacto decodificado por ZXing.
  Nombre 28 px y teléfono 18 px también conservan sus hashes de fuente C.
- Pool libre host al final del contrato del port **30.584 B**, frente a
  31.608 B antes: +1.024 B usados. No es una medida del heap ESP32.
- Nueva fuente: objeto Xtensa `-Os` **7.426 B text + 8 B BSS**. Receta fijada,
  fuente/licencia y hashes en [display-fonts](../../tools/display-fonts/README.md).

| Target | RAM estática B | Flash B | Delta vs extensión USB VIS-3 |
| --- | ---: | ---: | --- |
| Classic | 57.636 | 1.154.219 | Sin cambio |
| Waveshare producto | 123.268 | 1.488.279 | +200 RAM / +7.540 flash |
| Display diagnóstico | 123.804 | 1.493.703 | +200 RAM / +7.524 flash |

Tres builds correctos en 3:35,376. Classic sin símbolos PageHeader/fuente 14 px.
Buffer 9.600 B, pool 48 KiB, SPI 40 MHz y RGB565 sin cambios. La reducción de
duplicación entre las tres vistas compensa parte del código de la cabecera.

## Aceptación

Firmware diagnóstico cargado en COM6, sin borrar NVS. El recibo posterior conserva
`configured=true`, generación 1. SHA-256 del binario cargado:
`80e37e4cf559e82c99a98c7cb5e86063bd94890c3773486084bd7baf63b8fdb6`.

Ensayo USB de 65 s, 113 muestras LCD: 30 avances de página observados en orden,
una pulsación simulada despierta Placa sin avanzar y la siguiente abre Actividad.
Incluye cabeceras con/sin GPS DEMO. QR generado una sola vez; pool libre/bloque
mayor finales **38.084 B**. Máximo de servicio **45.863 µs**; límite superior del
histograma p95 mixto de navegación **50.000 µs**. Este histograma no mide FPS,
latencia óptica del botón ni p95 de refresco normal aislado. Las órdenes USB
ejercitan el port, no sustituyen pulsaciones físicas ni observación del panel.

Estado final: Actividad, luz encendida, demo desactivada y timeout desactivado.
GPS/tiras siguen ausentes. Logs y recibo locales en
`Platformio/Dog-RGB/artifacts/display-vis4/`; hashes en el manifiesto público.
**Lectura física de las nuevas cabeceras pendiente de respuesta del propietario.**
La Placa/QR y el ciclo físico anteriores ya estaban aceptados en VIS-3.

Acceso al AP, matriz óptica ampliada, V2/V3 y uso portátil siguen pendientes.
Siguiente incremento: VIS-5, captura temporal y un indicador animado opcional,
con comparación instantánea y prueba propia de movimiento/latencia.
