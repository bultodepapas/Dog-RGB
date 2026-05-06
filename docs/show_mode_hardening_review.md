# Auditoria del modo SHOW

Fecha de revision: 2026-05-06  
Alcance: firmware activo en `Platformio/Dog-RGB`, documentacion local y referencias tecnicas externas.  
Objetivo: auditar por que el modo SHOW puede sentirse poco aleatorio, y proponer fases de mejora sin aumentar el numero de funciones visibles al usuario.

---

## Resumen ejecutivo

La percepcion de que los colores no son suficientemente aleatorios esta parcialmente justificada, pero el problema principal no es solo el RNG. El modo SHOW actual es una demo determinista con color base aleatorio por efecto:

- El README promete "12 effects" y modos Show/Simple, pero no promete aleatoriedad total. Evidencia: `README.md:45-58`.
- La especificacion local dice que SHOW "recorre todos los efectos (IDs 0..11) cada 15 s" y usa "color base aleatorio por efecto". Evidencia: `docs/led_ui_spec.md:55-60`.
- El codigo implementa exactamente eso: entra en SHOW, arranca siempre en `show_effect_id = 0`, avanza `+1 mod EFFECT_COUNT`, y genera un color base nuevo solo al cambiar de efecto. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:613-628`.
- En ESP32 Arduino, `random()` usa hardware RNG por defecto en este core local, salvo que se llame `randomSeed()`. Evidencia: `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/WMath.cpp:32-63`. Por tanto, el punto debil mas visible no es "no hay seed", sino que el show tiene muy pocos eventos aleatorios y mucha simetria.

Diagnostico: SHOW hoy es correcto contra la especificacion, pero poco teatral. Se ve predecible porque el orden de efectos es fijo, ambas tiras comparten color y efecto, varios efectos ignoran `show_base`, y el portal solo muestra el efecto actual, no el color/base/temporizador que permitiria auditarlo en vivo.

---

## Investigaciones pertinentes

| ID | Investigacion | Evidencia | Conclusion |
| --- | --- | --- | --- |
| I01 | README como punto de partida | `README.md:45-58`, `README.md:84-88` | El proyecto declara LED UI con 12 efectos y Show/Simple; no define el nivel de aleatoriedad esperado. |
| I02 | Spec de SHOW | `docs/led_ui_spec.md:55-60` | La spec pide recorrido secuencial de efectos y color base aleatorio por efecto. El firmware cumple esa definicion. |
| I03 | Plan historico de SHOW | `docs/led_show_mode_plan.md:26-33`, `docs/led_show_mode_plan.md:85-100` | El plan original ya separaba "recorrer efectos" de "color aleatorio"; tambien advertia que FIRE/RAINBOW/GRADIENT_WAVE no dependen realmente del color base. |
| I04 | Arranque del modo | `Platformio/Dog-RGB/src/led/led_ui.cpp:613-621` | Cada entrada a SHOW empieza en efecto 0 y resetea estados A/B a cero. Esto hace repetible el primer tramo visual. |
| I05 | Seleccion de efecto | `Platformio/Dog-RGB/src/led/led_ui.cpp:623-628` | No hay seleccion aleatoria de efecto: el orden siempre es 0,1,2,...,11. Esto explica una parte grande de la sensacion "poco random". |
| I06 | Seleccion de color | `Platformio/Dog-RGB/src/led/led_ui.cpp:597-602` | El color si es aleatorio por HSV, con saturacion 200..255 y valor 180..255. Es una buena base, pero ocurre solo cada 15 s. |
| I07 | Efectos que ignoran color base | `Platformio/Dog-RGB/src/led/led_ui.cpp:421-435`, `Platformio/Dog-RGB/src/led/led_ui.cpp:428-430` | RAINBOW, GRADIENT_WAVE y FIRE no usan `show_base`. En 3 de 12 pasos el "color aleatorio" no se percibe como tal. |
| I08 | Simetria de dos tiras | `Platformio/Dog-RGB/src/led/led_ui.cpp:643-661` | Las tiras A/B usan el mismo efecto, mismo color y estados iniciales equivalentes. Si se busca show, esa simetria resta riqueza visual. |
| I09 | LEDs de estado y modo homogeneo | `Platformio/Dog-RGB/src/led/led_ui.cpp:635-673` | SHOW mantiene status LEDs, salvo modo homogeneo. Correcto para seguridad, pero visualmente reduce el segmento show de 24 a 22 LEDs por tira mientras status esta activo. |
| I10 | Observabilidad del portal dev | `Platformio/Dog-RGB/src/web/portal_http.cpp:319-323`, `Platformio/Dog-RGB/src/web/pages.cpp:1478-1480` | `/api/dev` expone solo `show.effect` y nombre. No expone color base, edad del efecto, proximo cambio, ni si esta en homogeneo. Dificulta probar la aleatoriedad. |
| I11 | Persistencia/config | `Platformio/Dog-RGB/src/config/runtime_config.cpp:150-166`, `Platformio/Dog-RGB/src/config/runtime_config.cpp:243-281` | `mode` persiste y SHOW esta validado, pero no hay parametros de show. Bien para no aumentar funciones; las mejoras deben ser internas/constantes. |
| I12 | Documentacion desactualizada | `docs/led_effects.md:7-16`, `docs/led_effects.md:42-52`, `Platformio/Dog-RGB/include/config.h:19-38` | `docs/led_effects.md` dice que los efectos estan en `Platformio/Dog-RGB/src/main.cpp` y lista defaults antiguos. El codigo actual esta en `Platformio/Dog-RGB/src/led/led_ui.cpp` y todos los rangos default usan JUGGLE. |
| I13 | RNG local del core ESP32 Arduino | `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/WMath.cpp:32-63` | En este entorno, `random()` usa `esp_random()` por defecto. No se encontro llamada a `randomSeed()` en el firmware activo. |
| I14 | Timing de LEDs RGBW | `Platformio/Dog-RGB/platformio.ini:12-14`, `Platformio/Dog-RGB/src/led/led_ui.cpp:28-29`, datasheet SK6812RGBW | El proyecto usa Adafruit NeoPixel con `NEO_GRBW + NEO_KHZ800`. SK6812RGBW trabaja con estructura de 32 bits y frecuencia tipica de 800 kHz; 2 x 24 LEDs cada 50 ms es razonable. |

---

## Evidencia externa relevante

- Espressif documenta que `esp_random()` obtiene datos del hardware RNG y que los numeros son verdaderamente aleatorios cuando Wi-Fi/BLE o una fuente interna de entropia estan activos; si no, deben considerarse pseudoaleatorios. Fuente: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/random.html>.
- El core Arduino ESP32 local usa hardware RNG por defecto para `random()`, y cambia a PRNG si se llama `randomSeed(seed != 0)`. Evidencia local: `~/.platformio/packages/framework-arduinoespressif32/cores/esp32/WMath.cpp:32-63`.
- Adafruit recomienda `setBrightness()` principalmente como ajuste de setup; no como efecto de animacion, porque es una operacion con perdida sobre el buffer. Fuente: <https://learn.adafruit.com/adafruit-neopixel-uberguide/arduino-library-use> y <https://adafruit.github.io/Adafruit_NeoPixel/html/class_adafruit___neo_pixel.html>.
- Espressif indica que ESP32-S3 tiene multiples canales RMT y soporta transmision multicanal; su FAQ recomienda ESP32-S3 para RMT por soporte DMA frente a interferencias de Wi-Fi/BLE. Fuentes: <https://docs.espressif.com/projects/esp-idf/en/release-v5.3/esp32s3/api-reference/peripherals/rmt.html> y <https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/rmt.html>.
- Seeed documenta el XIAO ESP32-S3 con ESP32-S3R8, Wi-Fi/BLE y 8 MB PSRAM/8 MB Flash. Fuente: <https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/>.
- El datasheet SK6812RGBW muestra datos de 32 bits, frecuencia tipica de 800 kHz y reset de 80 us. Fuente: <https://www.digikey.com/en/htmldatasheets/production/1857559/0/0/1/sk6812rgbw-specification>.

---

## Hallazgos tecnicos

### H1. SHOW no es aleatorio en el orden de efectos

Severidad: media.  
Impacto: alta percepcion de repeticion.

El firmware hace:

```cpp
show_effect_id = static_cast<uint8_t>((show_effect_id + 1) % EFFECT_COUNT);
```

Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:623-625`.

Esto cumple la spec, pero si el usuario espera "show" como demo viva, el orden fijo se aprende rapido. Ademas el primer efecto tras entrar al modo siempre es `SOLID`, porque `show_first_tick` fuerza `show_effect_id = 0`. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:614-618`.

Mejora sin nueva funcion: usar una bolsa barajada interna de IDs `0..11`, evitando repetir el efecto anterior al reiniciar la bolsa. El usuario seguiria teniendo "Show", no un modo nuevo.

### H2. El color aleatorio cambia poco

Severidad: media.  
Impacto: show visualmente estatico durante tramos largos.

`SHOW_EFFECT_MS = 15000`, y `show_base` solo se recalcula cuando cambia el efecto. Evidencia: `Platformio/Dog-RGB/include/config.h:90-94` y `Platformio/Dog-RGB/src/led/led_ui.cpp:623-627`.

Para efectos lentos o basados en base fija, 15 s puede sentirse como "el mismo color" demasiado tiempo. En SOLID/PULSE/BREATH/BPM esto es especialmente obvio.

Mejora sin nueva funcion: mantener 15 s por efecto, pero agregar variacion interna de paleta por subfases o interpolacion lenta hacia un segundo color aleatorio. No agrega controles ni nuevos modos.

### H3. Tres efectos no muestran el color aleatorio

Severidad: media.  
Impacto: inconsistencia entre expectativa y resultado.

`RAINBOW` y `GRADIENT_WAVE` generan HSV desde `state.hue`; `FIRE` usa `heat_color()`. Ninguno usa `show_base`. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:421-435`, `Platformio/Dog-RGB/src/led/led_ui.cpp:309-340`.

Esto no es bug funcional, pero si el informe de producto dice "colores aleatorios", durante 25% del ciclo actual esa promesa no se ve.

Mejora sin nueva funcion: randomizar `state.hue` al entrar a esos efectos y permitir tint/mezcla suave con `show_base` en GRADIENT_WAVE. Para FIRE, variar intensidad/sparking dentro de limites seguros puede dar variacion sin cambiar el catalogo.

### H4. Tira A y B son demasiado iguales

Severidad: baja-media.  
Impacto: menos sensacion de show.

El plan historico decidio que ambas tiras muestren el mismo efecto y mismo color. Evidencia: `docs/led_show_mode_plan.md:28-32`, `docs/led_show_mode_plan.md:42-44`. El codigo lo cumple. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:643-661`.

Para un collar con dos tiras, esa simetria es segura y legible, pero visualmente pobre. El welcome ya usa direccion inversa en B (`reverse`) y demuestra que el proyecto acepta diferencia visual controlada entre tiras. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:461-492`.

Mejora sin nueva funcion: conservar mismo efecto y color base, pero aplicar offset de fase/posicion en B o direccion invertida en efectos de movimiento. Es un endurecimiento visual interno, no un modo nuevo.

### H5. El estado LED reduce el area del show

Severidad: baja.  
Impacto: esperable por seguridad.

Con `LED_STATUS_COUNT = 2`, SHOW pinta Segmento B desde LED 2 hasta el final y luego repinta status. Evidencia: `Platformio/Dog-RGB/include/config.h:80-83` y `Platformio/Dog-RGB/src/led/led_ui.cpp:654-673`.

Esto es correcto para un collar de seguridad: Wi-Fi/GPS siguen visibles. Pero en una prueba de show puede parecer que hay "dos LEDs que no participan".

Mejora sin nueva funcion: documentar claramente que SHOW no pisa status salvo homogeneo, y exponer `homogeneous` en `/api/dev` para diagnostico.

### H6. Observabilidad insuficiente para auditar show en vivo

Severidad: media.  
Impacto: dificulta reproducir quejas.

`/api/dev` expone solo el efecto de show. Evidencia: `Platformio/Dog-RGB/src/web/portal_http.cpp:319-323`. La UI dev lo muestra como "Show effect". Evidencia: `Platformio/Dog-RGB/src/web/pages.cpp:1478-1480`.

No se puede ver desde el portal el RGB elegido, cuanto falta para cambiar, si la bolsa de efectos se reinicio, ni si el modo homogeneo esta activo.

Mejora sin nueva funcion: agregar diagnosticos dev-only: `show.base_rgb`, `show.elapsed_ms`, `show.remaining_ms`, `show.homogeneous`, `show.order_index`. No son funciones de usuario; son evidencia operativa.

### H7. Documentacion de efectos no coincide con el firmware activo

Severidad: media.  
Impacto: decisiones equivocadas de tuning.

`docs/led_effects.md` dice que la implementacion esta en `Platformio/Dog-RGB/src/main.cpp`; actualmente esta en `Platformio/Dog-RGB/src/led/led_ui.cpp`. Tambien lista defaults por rango que ya no coinciden con `config.h`, donde todos los rangos default usan `JUGGLE`. Evidencia: `docs/led_effects.md:7-16`, `docs/led_effects.md:42-52`, `Platformio/Dog-RGB/include/config.h:19-38`.

Mejora sin nueva funcion: corregir docs antes de tocar comportamiento; si el equipo prueba con una expectativa obsoleta, cualquier conclusion sobre show queda contaminada.

### H8. Brillo y white-channel estan bien orientados, pero conviene no abusar de `setBrightness()`

Severidad: baja.  
Impacto: robustez y calidad de color.

El codigo mantiene su propio buffer RGB y al hacer `show_leds()` extrae blanco como `min(r,g,b)` para alimentar SK6812 RGBW. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:203-220`. Eso es coherente con `NEO_GRBW`. Cambios frecuentes de `setBrightness()` no son recomendables segun Adafruit; el firmware solo lo usa en begin, welcome y cambios de config. Evidencia: `Platformio/Dog-RGB/src/led/led_ui.cpp:190-200`, `Platformio/Dog-RGB/src/led/led_ui.cpp:444-508`, `Platformio/Dog-RGB/src/led/led_ui.cpp:841-845`.

Mejora sin nueva funcion: mantener brillo global estable y hacer fades/transiciones en el buffer propio, no via `setBrightness()`.

---

## Fases de mejora

### Fase 0: Congelar evidencia y corregir documentacion

Objetivo: que todos midan lo mismo antes de cambiar visuales.

Acciones:
- Actualizar `docs/led_effects.md`: implementacion real en `Platformio/Dog-RGB/src/led/led_ui.cpp`, defaults actuales de `config.h`, nota explicita de efectos que ignoran color base.
- Agregar al informe de uso una frase clara: SHOW recorre efectos y usa color base aleatorio cuando el efecto lo permite.
- Crear checklist de prueba manual: entrada a SHOW, ciclo completo de 12 efectos, status activo, homogeneo, dual strip.

Criterio de salida:
- Documentos locales no contradicen el codigo.
- Una prueba de 3 minutos puede confirmar los 12 efectos sin abrir el codigo.

### Fase 1: Robustez interna de aleatoriedad

Objetivo: que SHOW deje de ser predecible sin agregar controles.

Acciones:
- Reemplazar orden `0..11` por bolsa barajada interna.
- Evitar repetir el efecto final de una bolsa como primer efecto de la siguiente.
- Inicializar SHOW con efecto aleatorio o primer elemento de bolsa barajada, no siempre SOLID.
- Randomizar `show_state_a.hue` y `show_state_b.hue` al entrar a RAINBOW/GRADIENT_WAVE.
- Limpiar buffers LED al cambiar de familias muy distintas si aparecen residuos visuales, especialmente despues de FIRE/COMET.

Criterio de salida:
- En 5 entradas consecutivas a SHOW, el primer efecto no es siempre el mismo.
- En 2 ciclos completos no se observan repeticiones inmediatas no deseadas.

### Fase 2: Mejorar lo visual sin ampliar el catalogo

Objetivo: mas bonito y mas show usando los mismos 12 efectos.

Acciones:
- Aplicar offsets de fase entre tira A y B para movimiento; por ejemplo, B inicia con `pos` desplazado o direccion inversa.
- Introducir transicion corta entre efectos: fade-out/fade-in o crossfade simple de 300..600 ms.
- Usar paletas curadas internas para `show_base` en vez de HSV puro siempre. Ejemplos: neon frio, alerta elegante, aurora, fiesta, blanco-calido-acento. Se elige internamente, no se expone en UI.
- En efectos basados en base fija, interpolar lentamente hacia un segundo color durante los 15 s.
- En FIRE, variar `intensity`/sparking dentro de una ventana segura o tintar sombras bajas para que no sea siempre el mismo fuego.

Criterio de salida:
- El usuario percibe variacion dentro de un mismo efecto, no solo al cambio de 15 s.
- Ambas tiras se sienten coordinadas, no duplicadas pixel a pixel.

### Fase 3: Diagnostico y hardening de ejecucion

Objetivo: poder demostrar que show esta haciendo lo que debe en hardware real.

Acciones:
- Extender `/api/dev` con diagnostico show-only: efecto, color RGB, elapsed/remaining, homogeneo, orden interno.
- Agregar logs seriales especificos al cambio de efecto, no en cada frame.
- Medir duracion real de `show_leds()` para A/B y confirmar margen contra `LED_UPDATE_MS = 50`.
- Validar con Wi-Fi AP activo, STA intentando y BLE activo, porque esas condiciones pueden afectar timing/percepcion.
- Mantener transiciones en buffer propio; no usar `setBrightness()` como animador.

Criterio de salida:
- Desde `/dev` se puede capturar evidencia de 12 cambios de efecto y colores.
- No hay parpadeo anomalo al cambiar de efecto con Wi-Fi/BLE activos.

### Fase 4: Validacion de producto

Objetivo: cerrar la mejora con criterios visibles, no solo tecnicos.

Acciones:
- Grabar 3 escenarios: mesa sin GPS, exterior con GPS, Wi-Fi apagado + homogeneo.
- Comparar firmware actual vs firmware mejorado con la misma duracion de prueba.
- Validar consumo/temperatura en show por 10 minutos.
- Validar legibilidad de status LEDs cuando no hay homogeneo.
- Actualizar README solo si la promesa de show cambia de "demo automatico" a "demo aleatorio curado".

Criterio de salida:
- El modo se siente menos repetitivo sin perder estado Wi-Fi/GPS.
- No hay regresion en speed/geofence/simple.

---

## Recomendacion priorizada

1. Corregir documentacion y defaults (`docs/led_effects.md`) antes de tocar firmware.
2. Agregar diagnostico dev de SHOW: color, elapsed/remaining y homogeneo.
3. Cambiar orden secuencial por bolsa barajada interna.
4. Randomizar fase/hue en efectos que hoy ignoran `show_base`.
5. Agregar offsets entre tiras A/B y transiciones cortas entre efectos.

La mejora mas costo/beneficio es la bolsa barajada + fase aleatoria + diagnostico. Eso atacaria directamente la queja de "no parece random" sin crear nuevos modos, pantallas o parametros de usuario.

---

## Riesgos y notas

- No se verifico en hardware fisico durante esta revision; el analisis es estatico sobre repo, docs y fuentes tecnicas.
- Hay un cambio local previo en `Platformio/Dog-RGB/src/gps/gps.cpp`; esta auditoria no lo toca.
- La aleatoriedad criptografica no es objetivo del modo SHOW. La meta es variacion visual. Para eso, el uso actual de `random()`/`esp_random()` es suficiente; el problema esta en la frecuencia y distribucion de decisiones visuales.
- Si se cambia el orden de efectos, actualizar la spec: ya no seria "recorre 0..11 en orden", sino "recorre los 12 efectos en orden barajado sin repeticion dentro del ciclo".
