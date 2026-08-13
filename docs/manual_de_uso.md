# Guía de Uso de Dog-RGB

**Estado:** traducción resumida, revisada el 2026-08-13. Para el detalle canónico consulta la [User guide](user-guide.md).

Dog-RGB funciona de forma local, sin app ni cuenta cloud. Los LEDs muestran el estado rápido y el portal Wi-Fi permite ver métricas, rutas, configuración y diagnóstico.

## Primer inicio

1. Enciende el collar al aire libre y con vista clara al cielo.
2. Conecta el teléfono a `DogRGB` usando la contraseña inicial `Dog12345`.
3. Abre el portal cautivo o visita `http://192.168.4.1/`.
4. Cambia la contraseña AP antes de usar el collar en lugares públicos.
5. Espera a que el indicador GNSS pase de azul pulsante a azul fijo.

No cargues el dispositivo mientras el perro lo usa. Revisa ajuste, temperatura, batería, cables y sellos; las luces no reemplazan correa, identificación ni un rastreador certificado.

## Estado LED

En todos los modos normales, incluido Simple, los primeros dos píxeles de cada tira están reservados:

| Indicador | Apariencia | Significado |
| --- | --- | --- |
| Wi-Fi | Verde fijo | STA conectado |
| Wi-Fi | Verde pulsante | Conexión STA en progreso |
| Wi-Fi | Amarillo fijo/pulsante | AP activo; el pulso indica cliente conectado |
| Wi-Fi | Rojo fijo | STA falló y existe fallback AP |
| Wi-Fi | Doble pulso ámbar | Estado Wi-Fi OFF explícito/experimental |
| GNSS | Azul fijo | Fix confiable |
| GNSS | Azul pulsante | Buscando o sin cumplir filtros de calidad |
| Ambos | Rojo rápido | Timeout crítico sin GNSS confiable ni STA |
| Ambos | Pulso rojo | Distancia Geofence válida igual o superior al límite máximo |

Los efectos usan el cuerpo semántico y no ocultan los indicadores. Day Mode conserva status aunque apaga el cuerpo. Una alerta System/Geofence interrumpe el crossfade del cuerpo en el siguiente tick LED.

## Páginas del portal

| Ruta | Uso |
| --- | --- |
| `/` | Métricas del día, sesión actual, tres sesiones terminadas y ruta/exportación |
| `/wifi` | Escaneo manual, credenciales STA, nombre/password del AP y estado |
| `/config` | Modo LED, brillo, potencia LED avanzada, Day Mode, filtros GNSS, Home, efectos y PIN opcional |
| `/dev` | Diagnóstico técnico, estimación de corriente, store/player de escenas, storage, parser GNSS, tiempos de loop y JSON |

Cuando STA está conectado, abre `http://dog-collar.local/` o la IP indicada por el portal. mDNS puede no funcionar en algunas redes.

## Modos

- **Speed:** diez rangos de velocidad, cada uno con efecto configurable para ambas tiras.
- **Geofence:** diez bandas según distancia al punto Home.
- **Show:** baraja las cuatro escenas integradas más cada escena de usuario elegible. Recorre cada una una vez por bolsa, evita repetir entre bolsas y cambia tras unos 30 segundos visibles usando la transición de la receta.
- **Simple:** aplica un efecto y color base al cuerpo y conserva status.

Las escenas integradas son **Alta visibilidad** (Chase + Safety Amber), **Calmado** (Breath + Night Red), **Activo** (Comet + Forest) y **Fiesta** (Rainbow + Pride). Existen cuatro slots adicionales que se pueden guardar, exportar e importar mediante la [API local](api-reference.md#led-scenes-api-v1). Aplicar manualmente es volátil: no cambia el modo ni escribe flash, y termina al cancelar, cambiar el modo o reiniciar. El portal embebido todavía no incluye editor gráfico de escenas.

El brillo por defecto es `77/255`. El limitador estimado viene activo con 1.000 mA totales, 200 mA base y perfil RGB/W de 20/20 mA. Está oculto en **Potencia LED (avanzado)** y protege ambas tiras con el mismo factor. Sigue siendo obligatorio medir el hardware real: no es un sensor ni certifica batería, boost, cableado o temperatura.

Day Mode es opcional y viene apagado. Si se activa, usa hora GNSS confiable y UTC-5 para apagar solo los píxeles de efecto entre 06:00 y 16:00. Si la hora falta o está stale, deja los efectos encendidos.

## Métricas y rutas

El dashboard muestra distancia de hoy, velocidad activa promedio, máxima válida, GNSS y sesiones. La ruta conserva hasta 1.440 puntos —aproximadamente dos horas a cinco segundos nominales— y se puede exportar como JSON, CSV o GeoJSON para la sesión actual o una de las tres sesiones terminadas.

## Política Wi-Fi actual

- Sin fix GNSS confiable, el firmware solicita disponibilidad del AP.
- Permanecer a `<= 2,0 km/h` durante unos dos minutos también puede activarlo.
- Un AP recién iniciado se mantiene al menos 15 minutos.
- La actividad del portal extiende la disponibilidad cinco minutos.
- Diez minutos sin clientes ni actividad permiten detener el SoftAP.
- Ese cierre por inactividad no apaga automáticamente todo el radio Wi-Fi.
- Los fallos usan reintentos limitados con backoff.

El escaneo es manual, devuelve hasta 20 redes únicas y puede interrumpir brevemente la conexión AP mientras cambia de canal.

## PIN opcional

Puedes proteger las acciones de escritura con un PIN de 4–8 dígitos. Viene desactivado para facilitar recuperación DIY. El PIN no cifra el tráfico ni oculta lecturas a alguien ya conectado a la red local. Un registro corrupto falla abierto para evitar bloquear permanentemente el portal.

El PIN también protege todas las mutaciones de escenas, incluso apply/cancel aunque sean volátiles.

## Restore defaults

La acción restaura la configuración runtime de LEDs, GNSS, AP y mDNS usando el storage validado. No borra escenas de usuario, rutas, métricas diarias, sesiones, credenciales STA, Home ni el PIN almacenado por separado. Las escenas tienen su propio banco A/B y se borran/importan por su API. Si cambias nombre o password del AP, deberás reconectarte.

## Solución rápida

| Problema | Revisión |
| --- | --- |
| No aparece `DogRGB` | Verifica alimentación/boot, prueba al aire libre y reinicia una vez |
| No abre el portal cautivo | Visita `http://192.168.4.1/` directamente |
| STA no conecta | Escanea de nuevo, revisa credenciales y usa el fallback AP |
| `dog-collar.local` falla | Usa la IP STA mostrada; la red puede bloquear mDNS |
| No hay métricas | Revisa fix, satélites, HDOP y edades GGA/RMC en `/dev` |
| LEDs hacen flicker o reinician | Detén la prueba y revisa 5 V, GND, level shifter, resistencias, capacitancia y temperatura |
| Una escritura devuelve `403 csrf` | Un cliente propio debe enviar `X-Dog-Portal` |
| Una escritura devuelve `401 locked` | Envía el PIN desde la UI o en `X-Dog-Pin` |
| Escenas devuelve `409 generation_conflict` | Lee de nuevo `/api/v1/led/scenes` y reintenta conscientemente con su generación actual |
| Solo funcionan las escenas integradas | Revisa `scene_store.health` y sus contadores en `/api/dev` antes de forzar recuperación |

Consulta la [referencia HTTP](api-reference.md) para payloads exactos.
