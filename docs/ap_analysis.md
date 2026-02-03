# Analisis de comportamiento del AP (Wi-Fi) - Dog-RGB

Fecha: 2026-02-03

## Alcance

Se reviso el flujo de Wi-Fi/AP en el firmware y la especificacion del portal.

Archivos revisados:
- `Platformio/Dog-RGB/src/main.cpp`
- `Platformio/Dog-RGB/include/config.h`
- `docs/wifi_portal_spec.md`
- `docs/wifi_portal_state_diagram.md`
- `docs/portal_config.md`

## Resumen ejecutivo

El AP no es fijo: su encendido y apagado dependen del GPS, la velocidad y la presencia de clientes conectados. Esto es intencional para ahorrar bateria, pero puede verse como “raro” si se espera un AP siempre visible. La politica actual tambien puede apagar el AP si el telefono entra en ahorro de energia o si el conteo de estaciones falla temporalmente. Ademas, cualquier cambio de SSID/password reinicia el AP con un delay corto, lo que corta la sesion actual.

## Funcionamiento actual del AP/STA (segun firmware)

- Arranque: `setup_wifi()` carga credenciales STA desde NVS y levanta AP si no hay SSID, o AP+STA si hay SSID. Referencias: `Platformio/Dog-RGB/src/main.cpp:3060`.
- Credenciales STA: `handle_wifi_save()` guarda SSID/pass y llama `start_sta_mode()`. Referencias: `Platformio/Dog-RGB/src/main.cpp:2967`.
- AP forzado ON sin GPS fix: `update_ap_policy()` activa AP si no hay fix. Referencias: `Platformio/Dog-RGB/src/main.cpp:3071`.
- AP ON por estacionario: si velocidad <= `AP_STATIONARY_ON_KPH` durante `AP_STATIONARY_MS`, se habilita AP. Referencias: `Platformio/Dog-RGB/include/config.h:91`, `Platformio/Dog-RGB/src/main.cpp:3078`.
- AP OFF por inactividad: si no hay clientes por `AP_IDLE_TIMEOUT_MS`, se apaga el AP y, si no hay STA, se pasa a `WIFI_OFF`. Referencias: `Platformio/Dog-RGB/include/config.h:86`, `Platformio/Dog-RGB/src/main.cpp:3122`.
- Reintentos STA: cada `WIFI_RETRY_INTERVAL_MS` se intenta reconectar a STA si hay SSID. Referencias: `Platformio/Dog-RGB/include/config.h:85`, `Platformio/Dog-RGB/src/main.cpp:3567`.
- Reinicio de AP por cambios: cambiar SSID o password dispara un reinicio del AP luego de `AP_RESTART_DELAY_MS`. Referencias: `Platformio/Dog-RGB/src/main.cpp:2899`, `Platformio/Dog-RGB/src/main.cpp:3590`.

## Hallazgos y posibles causas del “comportamiento raro”

1. AP “desaparece” mientras el perro se mueve
El firmware apaga el AP luego de `AP_IDLE_TIMEOUT_MS` sin clientes y lo mantiene apagado si hay GPS fix y no esta estacionario. Esto es parte de la politica de ahorro de energia, pero puede verse como un bug si esperas AP siempre visible. Referencias: `Platformio/Dog-RGB/src/main.cpp:3088`, `Platformio/Dog-RGB/src/main.cpp:3122`.

2. AP se mantiene encendido aun con movimiento
Si un cliente sigue conectado, el AP no se apaga aunque la velocidad suba, porque el apagado depende de inactividad (sin clientes), no de movimiento. Esto puede parecer contradictorio con la expectativa “AP off al moverse”. Referencias: `Platformio/Dog-RGB/src/main.cpp:3099`, `Platformio/Dog-RGB/src/main.cpp:3122`.

3. AP se apaga aunque el telefono crea estar conectado
El conteo de clientes usa `WiFi.softAPgetStationNum()`. Si el telefono entra en ahorro de energia o el conteo falla, `ap_station_count` puede bajar a 0 y el AP se apaga tras el timeout. Esto explicaria caidas “misteriosas”. Referencias: `Platformio/Dog-RGB/src/main.cpp:3099`, `Platformio/Dog-RGB/src/main.cpp:3122`.

4. AP se reinicia al guardar cambios de config
Cambios en SSID/password disparan un reinicio del AP con delay corto. El usuario queda desconectado de inmediato y puede percibirlo como inestabilidad. Referencias: `Platformio/Dog-RGB/src/main.cpp:2899`, `Platformio/Dog-RGB/src/main.cpp:3590`.

5. STA con credenciales incorrectas genera intentos recurrentes
Si el SSID/password son invalidos, el equipo intenta reconectar cada 10 s. Esto mantiene la radio ocupada y puede degradar la estabilidad del AP. Referencias: `Platformio/Dog-RGB/src/main.cpp:3567`.

6. Inconsistencia “AP abierto” en la UI
Si el AP queda con password vacio y en la UI se desmarca “AP abierto” sin ingresar password, el firmware no cambia la clave y el AP sigue abierto. Esto puede confundirse con un error de configuracion. Referencias: `Platformio/Dog-RGB/src/main.cpp:2871`, `Platformio/Dog-RGB/src/main.cpp:2888`.

## Pruebas sugeridas para aislar el problema

1. Conectarse al AP y dejar el telefono en reposo 5-6 min sin trafico. Ver si el AP se apaga. Esto valida el timeout y el conteo de clientes.
2. Probar en movimiento con GPS fix estable. Confirmar si el AP desaparece tras 5 min sin clientes.
3. Forzar GPS sin fix (interior). Verificar si el AP queda establemente encendido (politica “AP force on”).
4. Configurar SSID/pass incorrecto y observar logs: verificar si el AP se vuelve inestable por reintentos STA.
5. Cambiar SSID/pass desde `/config` y verificar que el reinicio del AP sea el motivo de la desconexion.

## Datos que ayudarian a confirmar

- En que escenario exacto “se comporta raro” (se apaga, no aparece, se reinicia, no acepta clientes).
- Estado de GPS al momento del problema (fix/no fix).
- Si habia un cliente conectado y activo.
- Si hay credenciales STA configuradas.
- Logs seriales alrededor del evento (los prints de Wi-Fi/LED ya existen en `loop()`).

