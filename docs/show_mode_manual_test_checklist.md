# Checklist manual del modo SHOW

Fecha base: 2026-05-06
Objetivo: validar el comportamiento actual de SHOW despues de Fase 1.

---

## Preparacion

- Firmware cargado desde `Platformio/Dog-RGB`.
- LED UI habilitado (`LED_UI_ENABLED = true`).
- Tiras conectadas segun `LED_STRIP_MODE` y `LED_STRIP_COUNT`.
- Portal accesible por AP (`http://192.168.4.1`) o STA (`http://dog-collar.local`).
- Brillo configurado en un valor seguro para prueba continua.

Registrar antes de empezar:
- Build visible en `/dev`.
- Modo inicial.
- Estado GPS: fix / sin fix.
- Estado Wi-Fi: AP, STA, AP+STA u OFF.
- Numero de clientes AP.

---

## Prueba 1: entrada a SHOW

1. Abrir `/config`.
2. Cambiar modo a `Show`.
3. Guardar.
4. Abrir `/dev`.
5. Confirmar que `LED > Modo` muestra `show`.
6. Confirmar que `LED > Show effect` muestra nombre e ID.

Resultado esperado:
- La demo inicia en Segmento B.
- LED0/LED1 siguen mostrando estado Wi-Fi/GPS salvo modo homogeneo.
- El primer efecto no esta fijado a SOLID; depende de la bolsa barajada interna.

---

## Prueba 2: ciclo completo de efectos

Mantener SHOW activo por al menos 3 minutos.

Registrar cada cambio de efecto:

| Cambio | Tiempo aproximado | Efecto observado | ID observado | Notas |
| --- | --- | --- | --- | --- |
| 1 | 00:00 |  |  |  |
| 2 | 00:30 |  |  |  |
| 3 | 01:00 |  |  |  |
| 4 | 01:30 |  |  |  |
| 5 | 02:00 |  |  |  |
| 6 | 02:30 |  |  |  |
| 7 | 03:00 |  |  |  |
| 8 | 03:30 |  |  |  |
| 9 | 04:00 |  |  |  |
| 10 | 04:30 |  |  |  |
| 11 | 05:00 |  |  |  |
| 12 | 05:30 |  |  |  |

Resultado esperado:
- Los 12 IDs aparecen una vez antes de repetir bolsa.
- Al iniciar una bolsa nueva, el primer efecto no debe ser igual al ultimo efecto de la bolsa anterior.
- Cada efecto dura aproximadamente 30 s.
- Cada cambio de efecto tiene una transicion breve, sin corte brusco.
- En efectos que usan color base, debe notarse evolucion de color dentro de los 30 s.
- El color base cambia al pasar de efecto, aunque RAINBOW, FIRE y GRADIENT_WAVE no lo reflejan directamente.

---

## Prueba 3: status activo

Con SHOW activo y Wi-Fi/GPS en estados conocidos:

- Confirmar LED0 segun estado Wi-Fi/AP.
- Confirmar LED1 segun estado GPS.
- Confirmar que el resto de la tira sigue en demo.

Resultado esperado:
- Segmento A no se mezcla con SHOW mientras no haya modo homogeneo.
- Segmento B mantiene el efecto actual.

---

## Prueba 4: modo homogeneo

Condicion: Wi-Fi OFF y GPS OK estable por mas de `WIFI_OFF_GPS_FIX_MS` (default 5 min).

Pasos:
1. Activar SHOW.
2. Dejar que el sistema cumpla la condicion de homogeneo.
3. Observar LED0/LED1 y Segmento B.

Resultado esperado:
- SHOW se aplica a toda la tira.
- Los LEDs de estado quedan pisados por el efecto actual.

---

## Prueba 5: doble tira

Si `LED_STRIP_MODE = 2`:

- Confirmar que tira A y tira B muestran el mismo efecto.
- Confirmar que no hay una tira congelada o desfasada por error.
- Registrar si la simetria visual parece pobre o repetitiva.

Resultado esperado:
- Ambas tiras usan el mismo efecto SHOW en todo momento.
- Ambas tiras usan los mismos parametros internos de SHOW; no debe aparecer un efecto distinto en una sola tira.
- La simetria actual es comportamiento esperado, no fallo.

---

## Criterio de cierre

La Fase 1 queda validada si:

- El modo SHOW puede activarse desde `/config`.
- `/dev` muestra `mode = show` y el efecto actual.
- Se observan 12 efectos sin repeticion dentro de la bolsa.
- Se observan transiciones breves entre efectos.
- Se observa variacion de color dentro de efectos que usan color base.
- Ambas cintas mantienen el mismo efecto en todo momento.
- Status LEDs se conservan fuera de homogeneo.
- Homogeneo pisa toda la tira cuando corresponde.
- Cualquier diferencia visual se registra como hallazgo, no como cambio aplicado.
