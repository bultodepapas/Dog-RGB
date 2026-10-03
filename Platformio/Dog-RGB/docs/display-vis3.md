# VIS-3 — Placa persistida en navegación

LVGL 8.4.0 / Arduino_GFX 1.6.7 / SPI 40 MHz, buffer RGB565 de 9.600 B y
pool de 48 KiB. `IdentityView` y sus fuentes ya se enlazan en producto Display
y diagnóstico etapa 3. Classic conserva exclusión de `display/`.

## Contrato

- Con nombre/teléfono válidos: arranque Placa y ciclo **Placa → Actividad →
  Wi-Fi → Estado → Placa**, indicadores 1/4 a 4/4. QR desactivado conserva
  la página de nombre/teléfono. Nombre largo usa el layout textual existente.
- Sin identidad: arranque Actividad, ciclo e indicadores 1/3 a 3/3 anteriores.
- Guardar no cambia la página actual ni enciende el backlight. Cambia el texto
  visible en la siguiente ejecución del servicio; QR se regenera solo al
  cambiar su payload. Clics, muestreo y guardado equivalente no regeneran QR.
- Borrar mientras se muestra Placa retira ese contenido y selecciona Actividad;
  si estaba oscuro, permanece oscuro. Es la excepción explícita a retener página.
- BOOT corto en oscuro despierta la página retenida; el siguiente avanza. Largo
  y rebote conservan el contrato anterior. Sin atajo nuevo ni autoavance.
- Timeout sigue desactivado al arrancar. `i/o` permiten el ensayo de 30 s;
  no se añade una política portátil ni una animación al QR.

`portal_http::begin()` carga el store antes de `display::begin()` en el mismo
loop Arduino. `capture_identity()` copia 49+17 bytes y canal desde RAM;
no lee NVS. El port compara esa copia antes de validar/cambiar objetos.
La captura no retiene referencias al store. Durante pausa del servicio la copia
se actualiza, pero no hay transferencias SPI hasta reanudar.

## Diagnóstico y configuración

`p` selecciona Placa si está configurada, o Actividad si no lo está. Se mantiene
`a/c/e`, `n` equivale a la liberación BOOT aceptada. `[LCD]` incorpora
`page=identity` y `qr_generations`: contador de generaciones durante ese arranque,
no se pone a cero con `r`. No imprime nombre, teléfono ni payload por serial.

Configurar desde `/config`, sección **Placa de mi perro**, con teléfono
internacional explícito y canal WhatsApp/llamada/desactivado. La identidad real
no se compila como fixture ni como valor por defecto. Para lectura del QR no
hace falta conectarse al AP. El portal es la vía de producto; el diagnóstico
etapa 3 ofrece ahora [configuración USB](identity-usb.md) si el AP no es accesible.
Contrato de persistencia/HTTP: [API](../../../docs/display-identity-api.md).

## Verificación reproducible

Desde raíz:

```powershell
python tools/display-simulator/render.py
tools/display-simulator/build/qr-venv/Scripts/python.exe tools/display-simulator/render_port.py
```

El segundo usa el entorno de [decoder fijado](../../../tools/display-simulator/README.md).
Doce CTest incluyen el arranque separado con identidad persistida y el servicio
real junto al store/adapter reales, con Arduino/SPI/Preferences de prueba.
Once capturas del framebuffer SPI comprueban cinco QR y seis vistas sin QR.
Incluye copia sin alias, fallo de guardado, renombrar sin regenerar, edición en
oscuro, 30 ciclos de cuatro páginas, 30 despertares en Placa, retorno desde
texto/barras, borrado y memoria estable. Los tiempos de SPI falso no son medidas.

Desde `Platformio/Dog-RGB`, compilar Classic, `waveshare_lcd169` y
`waveshare_lcd169_displaycheck`, suite Python y contratos antes de cargar etapa 3.
La aceptación física exige lectura/escaneo del panel, payload correcto, BOOT,
reinicio con contacto retenido y medidas de memoria/servicio. GPS/tiras y uso
portátil mantienen sus pruebas separadas.

Resultados: [baseline VIS-3](../../../docs/baselines/display-vis3-2026-09-12.md).
