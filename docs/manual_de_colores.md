# Referencia de Colores LED

**Estado:** traducción de conveniencia de [LED Color and Status Reference](color-reference.md), revisada el 2026-08-13.

La configuración por defecto usa dos tiras RGBW de 24 píxeles. En todos los modos normales los píxeles `0..1` muestran status y `2..23` muestran el efecto. El brillo global inicial es `77/255`.

## Indicadores

| Color/patrón | Significado |
| --- | --- |
| Verde fijo | STA conectado |
| Verde pulsante | STA conectando |
| Amarillo fijo/pulsante | AP activo; pulso con cliente AP |
| Rojo fijo | Fallo STA con fallback AP |
| Doble pulso ámbar | Estado Wi-Fi OFF explícito |
| Azul fijo | Fix GNSS confiable |
| Azul pulsante | Buscando o sin cumplir filtros GNSS |
| Rojo rápido en ambos | Timeout crítico de conectividad/fix |
| Pulso rojo en ambos | Límite máximo de Geofence alcanzado/superado |

Day Mode apaga los píxeles de efecto, no los indicadores.

## Colores por velocidad

| Rango | Velocidad por defecto | RGB base | Color |
| ---: | --- | --- | --- |
| 1 | `<= 2 km/h` | `(0, 60, 60)` | Cian |
| 2 | `> 2–4 km/h` | `(0, 60, 35)` | Cian-verde |
| 3 | `> 4–6 km/h` | `(0, 60, 0)` | Verde |
| 4 | `> 6–8 km/h` | `(25, 60, 0)` | Verde lima |
| 5 | `> 8–10 km/h` | `(60, 60, 0)` | Amarillo |
| 6 | `> 10–12 km/h` | `(60, 45, 0)` | Ámbar |
| 7 | `> 12–14 km/h` | `(60, 30, 0)` | Naranja |
| 8 | `> 14–16 km/h` | `(60, 20, 0)` | Naranja intenso |
| 9 | `> 16–18 km/h` | `(60, 10, 0)` | Rojo-naranja |
| 10 | `> 18 km/h` | `(60, 0, 0)` | Rojo |

Velocidades reportadas por encima de 40 km/h se rechazan como picos y no cuentan como actividad válida.

## Geofence y efectos

Geofence divide `fence_max_m` en diez bandas y reutiliza la paleta de cian a rojo. Con el máximo inicial de 300 m, cada banda mide 30 m. Sin Home muestra respiración ámbar; sin fix confiable usa rainbow.

La mayoría de efectos usa el RGB base. `BREATH`, `CHASE`, `COMET`, `RAINBOW` y `GRADIENT_WAVE` pueden consumir una de ocho paletas RGBW curadas; `FIRE` conserva su calor interno. Consulta el [catálogo de efectos](led_effects.md).

## Escenas integradas

| ID/clave | Nombre | Efecto/paleta | Base/acento | Nivel de cuerpo | Transición |
| --- | --- | --- | --- | ---: | ---: |
| `1/high_visibility` | Alta visibilidad | Chase / Safety Amber | `#FF5000` / `#FFDCA0` | 255 | 400 ms |
| `2/calm` | Calmado | Breath / Night Red | `#780000` / `#FF280A` | 110 | 900 ms |
| `3/active` | Activo | Comet / Forest | `#005A19` / `#64FFAA` | 200 | 500 ms |
| `4/party` | Fiesta | Rainbow / Pride | `#C800C8` / `#00C8FF` | 180 | 650 ms |

Show baraja estas cuatro escenas y cualquier slot de usuario elegible. El nivel es relativo al brillo global y escala solo el cuerpo antes del crossfade; nunca aumenta por encima del brillo/presupuesto configurado ni atenúa status o alertas. “Alta visibilidad” describe la estética y no es una certificación de seguridad.
