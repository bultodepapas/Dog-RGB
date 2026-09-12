# VIS-1b — IdentityView y tipografía

2026-09-12. **Implementado/validado en host; no cargado a COM6.**
[Plan activo](../PLANS/2026-09-12_display-visual-identity.md).

`IdentityView` comparte el código con firmware y reutiliza `ContactQr` de VIS-1a.
Composición A: nombre en una línea, QR inmóvil, teléfono completo y canal.
B: nombre centrado de hasta cinco líneas y teléfono, sin QR; se activa para
nombre largo, QR desactivado o datos inválidos. Fondo negro; área segura
192×244 en (24,20). Nombre Montserrat 600 de 28 px; teléfono 18 px; ambos 4 bpp.
Nombre/teléfono permanecen estáticos. Sin nuevas pantallas físicas ni animaciones.

## Contrato y evidencia

- `format_pet_name`: 48 bytes UTF-8/24 puntos de código; letras ASCII y Latin-1,
  dígitos, espacio, guion y apóstrofo. Rechaza UTF-8 malformado, caracteres no
  soportados y espacios inicial/final/dobles. Tildes descompuestas devuelven
  `NeedsNormalization`; no se normalizan ni sustituyen silenciosamente en MCU.
  VIS-2 debe normalizar NFC en el editor y repetir validación en firmware.
- Cada letra admitida se verifica contra los glifos reales y se repite 24 veces
  para comprobar ancho/altura. Casos específicos cubren cinco palabras anchas,
  48 bytes, ñ, tildes, límites, emoji, sobrelongitud y UTF-8 inválido.
- Verificación de límites y solapamientos entre todos los objetos visibles;
  teléfono y nombre completos, sin marquesina ni elipsis. Cambio exclusivo de
  nombre no regenera QR. 100 updates equivalentes no generan flush.
- 30 ciclos de layouts, contacto inválido y recuperación sin caída de pool;
  destrucción/recreación explícita. Las tres vistas I6d se mantienen asignadas
  durante el ensayo de memoria, con sus textos iniciales vacíos.
- **7/7 CTest y 147 tests Python OK.** Trece fixtures públicas por variante
  tipográfica y una captura local adicional de FREYA con el teléfono solicitado.
  ZXing 2.3.0 comprueba payload exacto/cantidad: cinco QR y ocho fallbacks por
  variante pública; la captura local también decodifica exactamente su enlace.
- Classic, Display producto y Display diagnóstico compilados correctamente.
  La imagen física I6d y sus tres páginas permanecen sin cambios.

[Manifest/capturas 4 bpp](../assets/display-vis1b/manifest.json),
[nombre corto](../assets/display-vis1b/identity-short.png),
[nombre largo](../assets/display-vis1b/identity-long.png),
[caso ancho](../assets/display-vis1b/identity-wordwrap.png),
[ñ](../assets/display-vis1b/identity-enye.png).
Los datos reales solo se usan en el output local ignorado por Git.

## Recursos

| Medición | Resultado |
| --- | ---: |
| `sizeof(IdentityView)`, buffers incluidos, host / Xtensa | 3.264 / 3.240 B |
| Pool libre: tres vistas I6d / construcción / primer QR / estable | 33.840 / 32.064 / 31.968 / 31.968 B |
| Incremento de pool estable | 1.872 B, menor que el presupuesto de 8 KiB |
| Fuentes 4 bpp, objetos Xtensa `-Os`, sección text | 24.059 B |
| Alternativa 2 bpp, misma medición | 12.766 B |
| Cachés BSS de las dos fuentes | 16 B adicionales |

`sizeof` y pool son recursos diferentes; no sumar buffers otra vez al tamaño
de la instancia. Pool y estabilidad son evidencia host; stack/heap mínimo,
bloque mayor, latencia, lectura óptica y energía necesitan la integración física.
La [receta de fuentes](../../tools/display-fonts/README.md) conserva original,
licencia OFL, versiones/lockfile, hashes y alternativas. Se regeneró dos veces
con hashes idénticos. 4 bpp ofrece curvas/diagonales más suaves; 2 bpp sigue
disponible si el presupuesto final requiere recortar.

Los tamaños de firmware siguen en la referencia I6d (Classic RAM/flash
57.636/1.151.879 B; Display 119.644/1.435.675 B; diagnóstico
119.652/1.437.399 B). Todavía no hay una instancia enlazada de IdentityView en
el port; el linker elimina código/fuentes no usados. Estas compilaciones no
demuestran el coste final de identidad activada ni aceptación física.

## Reproducción y siguiente incremento

`python tools/display-simulator/render.py` ejecuta los siete contratos.
Con el entorno QR: `render_identity.py` genera/decodifica la variante 4 bpp;
`--bpp 2` usa los assets alternativos de la receta. Parámetros `--name` y
`--phone` permiten una vista local sin guardar esos valores en fixtures.
Procedimiento: [simulador](../../tools/display-simulator/README.md#identity-layouts-vis-1b).

**VIS-2:** registro de identidad separado A/B + CRC, API local y editor con NFC,
contrato/capabilities y recuperación tras error/reinicio. VIS-3 enlaza la cuarta
página y valida el QR real, BOOT y despertar. V2/V3 con GPS/tiras siguen abiertos.
