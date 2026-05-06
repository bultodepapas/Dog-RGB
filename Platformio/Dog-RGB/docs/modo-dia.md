# Modo DIA

Modo DIA reduce consumo apagando los LEDs de efectos durante horas de luz, sin detener rastreo ni alertas.

## Comportamiento

| Condicion | LEDs de efectos | LEDs de alerta | GPS/track | Wi-Fi/portal |
|---|---|---|---|---|
| DIA desactivado | normal | normal | normal | normal |
| DIA activado, 06:00-15:59 local | apagados | normal | normal | normal |
| DIA activado, fuera de ventana | normal | normal | normal | normal |
| DIA activado, sin hora GPS confiable | normal | normal | normal | normal |
| Bienvenida de arranque | normal | normal | normal | normal |

La ventana es inclusiva en inicio y exclusiva en fin: `06:00 <= hora < 16:00`.

## Fuente De Hora

El firmware usa hora de sentencias GPS RMC. La hora RMC se trata como UTC y se convierte con:

- `DAY_MODE_TZ_OFFSET_MIN = -300`
- `DAY_MODE_START_MIN = 360`
- `DAY_MODE_END_MIN = 960`
- `DAY_MODE_TIME_STALE_MS = 300000`

La hora se considera confiable solo si:

- existe fecha GPS (`current_date_yyyymmdd != 0`);
- se recibio una hora de RMC con fix confiable;
- esa hora no esta vencida por mas de `DAY_MODE_TIME_STALE_MS`.

Si la hora no es confiable, `day_mode::state_name()` devuelve `waiting_time` y los efectos no se apagan.

## Arquitectura

Archivos principales:

- `include/config.h`: constantes de ventana DIA y zona horaria.
- `include/config/runtime_config.h`: campo persistente `day_mode_enabled`.
- `src/config/runtime_config.cpp`: version NVS `5`, carga, guardado y migracion.
- `include/gps/gps.h` y `src/gps/gps.cpp`: API de hora GPS confiable.
- `include/power/day_mode.h` y `src/power/day_mode.cpp`: evaluador sin efectos laterales.
- `src/led/led_ui.cpp`: compuerta que apaga solo cuerpo LED y conserva estado.
- `src/web/portal_http.cpp`: JSON de config/status/dev.
- `src/web/pages.cpp`: toggle, dashboard y diagnostico.

## Prioridad En LED UI

El orden intencional en `update_led_ui()` es:

1. Si la bienvenida esta activa, `update_welcome()` corre y retorna.
2. Si el modo visual es `show`, `update_show_mode()` aplica DIA dentro de ese flujo.
3. Si el modo visual es `simple`, `update_simple_mode()` aplica DIA dentro de ese flujo.
4. En `speed`/`geofence`, DIA se evalua antes de `homogeneous_mode`.

Esto garantiza que:

- la bienvenida de arranque sigue funcionando aunque DIA este activo;
- DIA gana sobre `homogeneous_mode`, que de lo contrario podria pintar toda la tira desde el indice `0`;
- los LEDs de alerta se pintan despues de limpiar el cuerpo.

## API

`GET /api/config` incluye:

```json
{
  "day_mode": {
    "enabled": true,
    "start_min": 360,
    "end_min": 960,
    "tz_offset_min": -300
  }
}
```

`POST /api/config` acepta:

```json
{
  "day_mode": {
    "enabled": true
  }
}
```

`GET /api/status` y `GET /api/dev` exponen estado runtime:

- `enabled`
- `active`
- `state`: `disabled`, `waiting_time`, `active`, `outside_window`
- `time_available`
- `local_min`

`GET /api/dev` tambien expone `start_min`, `end_min` y `tz_offset_min`.

## Pruebas

Prueba estatica de contrato:

```powershell
python -m unittest test.test_day_mode_static -v
```

Build firmware:

```powershell
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -e seeed_xiao_esp32s3
```

Casos manuales recomendados:

- DIA activado sin fix/hora: estado `waiting_time`, efectos normales.
- DIA activado entre 06:00 y 15:59 local: cuerpo apagado, alertas visibles.
- DIA activado desde 16:00: efectos vuelven sin reiniciar.
- Bienvenida al boot: corre completa aunque DIA este activo.
