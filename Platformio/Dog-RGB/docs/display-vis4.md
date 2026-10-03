# VIS-4 — Cabecera del perro y tema compartido

Actividad, Wi-Fi y Estado usan `ui_theme` para página negra, tipografía,
etiquetas, divisores y setters que no invalidan valores iguales. `PageHeader`
comparte nombre, numeración y marca GPS DEMO; objetos creados una sola vez,
sin animaciones, contenedores extra ni cambios de geometría del área segura.

## Contenido y reglas

- Nombre del perfil validado, Montserrat 600 de 14 px/4 bpp, tono `#D8E5EC`;
  `RGB DOG` si no hay identidad configurada. Se actualiza también al renombrar
  sin cambiar teléfono/canal ni disponibilidad de la página Placa.
- Nombre completo si cabe en 164 px. Con GPS demo, ancho 90 px para reservar
  espacio a `GPS DEMO` y a la página. Abreviación explícita con `...` por ancho
  medido, cortando únicamente entre caracteres UTF-8. Placa conserva nombre
  completo, contacto y QR aceptados; ninguna abreviación se guarda en NVS.
- `GPS DEMO` permanece visible en las tres vistas del propietario cuando se
  simulan muestras GPS. No significa que Wi-Fi o política LED sean simulados.
  Numeración tres/cuatro páginas y contraste no dependen del color.
- Fuente de 14 px incluye el mismo alfabeto latino/ñ/tildes de identidad más
  punto para la abreviación. Generación y licencia compartidas con las fuentes
  de Placa; sin ampliar glifos de SSID, sin nuevos validadores ni dependencias.
- Actividad conserva distancia/fecha/velocidad; Wi-Fi conserva AP/STA/SSID/IP;
  Estado conserva validez GPS y política LED efectiva. Márgenes 24/20 y 192×244,
  negro, indicadores y textos de estado se mantienen. No se infieren salud,
  batería, descanso ni conexión a Internet.

## Verificación

`python tools/display-simulator/render.py`: doce contratos, ahora 21 capturas
de vistas más once del port mediante `render_port.py`. Siete fixtures nuevos
de cabecera: nombre corto, demo, tilde, ñ, nombre largo, mayúsculas anchas e
inválido. Se comprueban glifos latinos, 24 caracteres extremos, ancho/separación,
100 actualizaciones iguales sin flush y 30 ciclos con memoria estable.

Los siete renders del port correspondientes a Placa siguen idénticos a VIS-3.
Tres vistas del port comprueban la propagación del nombre desde el store.
Mismos LVGL 8.4.0/Arduino_GFX 1.6.7, SPI 40 MHz, buffer 9.600 B y pool 48 KiB.

Resultados/capturas/tamaños: [baseline VIS-4](../../../docs/baselines/display-vis4-2026-09-12.md).
Transición medida sigue VIS-5; acceso al AP y aceptación conjunta V2/V3 abiertos.
