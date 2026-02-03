# Portal Config (Runtime) - Referencia

Esta documentacion describe el JSON, validaciones, UI y persistencia tal como esta implementado en el firmware actual.

---

## Endpoints

- `GET /config` (HTML UI)
- `GET /api/config`
- `POST /api/config` (JSON)
- `POST /api/config/reset`
- `GET /api/home`
- `POST /api/home/set`
- `POST /api/home/clear`

---

## JSON (GET /api/config)

```
{
  "version": 3,
  "mode": "speed",
  "fence_max_m": 300,
  "led": {
    "brightness": 77
  },
  "speed_ranges_kph": [2.0, 4.0, 6.0, 8.0, 12.0, 16.0, 22.0, 28.0, 34.0],
  "effects": {
    "range1": {"a": 0, "b": 0, "speed": 40, "intensity": 80},
    "range2": {"a": 1, "b": 1, "speed": 58, "intensity": 95},
    "range3": {"a": 2, "b": 2, "speed": 76, "intensity": 110},
    "range4": {"a": 3, "b": 3, "speed": 94, "intensity": 125},
    "range5": {"a": 5, "b": 5, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 7, "speed": 130, "intensity": 155},
    "range7": {"a": 8, "b": 8, "speed": 148, "intensity": 170},
    "range8": {"a": 9, "b": 9, "speed": 166, "intensity": 180},
    "range9": {"a": 11, "b": 11, "speed": 184, "intensity": 190},
    "range10": {"a": 10, "b": 10, "speed": 200, "intensity": 200}
  },
  "wifi": {
    "ap_ssid": "dog",
    "has_ap_pass": true,
    "mdns": "dog-collar"
  }
}
```

Notas:
- `has_ap_pass` indica si hay password configurado. No se devuelve el password.

---

## JSON (POST /api/config)

```
{
  "version": 3,
  "mode": "speed",
  "fence_max_m": 300,
  "led": {"brightness": 77},
  "speed_ranges_kph": [2.0, 4.0, 6.0, 8.0, 12.0, 16.0, 22.0, 28.0, 34.0],
  "effects": {
    "range1": {"a": 0, "b": 0, "speed": 40, "intensity": 80},
    "range2": {"a": 1, "b": 1, "speed": 58, "intensity": 95},
    "range3": {"a": 2, "b": 2, "speed": 76, "intensity": 110},
    "range4": {"a": 3, "b": 3, "speed": 94, "intensity": 125},
    "range5": {"a": 5, "b": 5, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 7, "speed": 130, "intensity": 155},
    "range7": {"a": 8, "b": 8, "speed": 148, "intensity": 170},
    "range8": {"a": 9, "b": 9, "speed": 166, "intensity": 180},
    "range9": {"a": 11, "b": 11, "speed": 184, "intensity": 190},
    "range10": {"a": 10, "b": 10, "speed": 200, "intensity": 200}
  },
  "wifi": {
    "ap_ssid": "dog",
    "ap_pass": "Dog123456789",
    "ap_open": false,
    "mdns": "dog-collar"
  }
}
```

Notas:
- `ap_pass` es opcional. Si no se envia, se mantiene el valor actual.
- Si `ap_open` es `true`, el AP se guarda sin password.
- `mode`: `"speed"` (default), `"geofence"` o `"show"`.
- `fence_max_m`: distancia maxima en metros (se divide en 10 rangos iguales).
- `mode: "show"` habilita demo de efectos en Segmento B (homogeneous puede pisar todo).

---

## API Home

`GET /api/home`:
```
{
  "home_set": true,
  "home_source": "auto",
  "home_lat": -34.6037,
  "home_lon": -58.3816,
  "gps_fix": true,
  "current_lat": -34.6037,
  "current_lon": -58.3816,
  "distance_m": 12.5
}
```

`POST /api/home/set`:
- Usa el GPS actual para setear home.
- Error si no hay fix: `{"status":"error","reason":"no_gps"}`.

`POST /api/home/clear`:
- Borra el home guardado.

---

## Validaciones y errores

Validaciones:
- `brightness`: 1..255
- `mode`: `speed` | `geofence` | `show`
- `fence_max_m`: 50..5000
- `speed_ranges_kph`: 9 valores > 0, estrictamente ascendentes
- `effects`: `range1..range10` presentes
- `effect a/b`: 0..11
- `speed/intensity`: 0..255
- `ap_ssid`: 1..32
- `ap_pass`: >= 8 si se envia y `ap_open` es `false`
- `mdns`: 1..32, solo letras, numeros y guiones

Errores (400) con `{"status":"error","reason":"..."}`:
- `no body`
- `bad json`
- `brightness`
- `mode`
- `fence_max`
- `ranges`
- `ranges value`
- `ranges order`
- `effects`
- `effect values`
- `effect id`
- `ssid`
- `pass`
- `mdns`

Respuesta OK:
- `{"status":"ok","wifi_restart":true/false}`

---

## Aplicacion y persistencia

- Namespace NVS: `dogrgb_cfg`
- Claves:
  - `ver` (uint8)
  - `brightness` (uint8)
  - `ranges` (9 floats)
  - `effects` (10 entradas: effect_a, effect_b, speed, intensity)
  - `mode` (uint8)
  - `fence_max` (uint16)
  - `ap_ssid` (string)
  - `ap_pass` (string, puede estar vacio)
  - `mdns` (string)
  - `home_set` (uint8)
  - `home_lat` (float)
  - `home_lon` (float)
  - `home_src` (uint8, 1=auto, 2=manual)

Al guardar:
- Se valida, se guarda en NVS y se aplica en caliente.
- Si cambian `ap_ssid` o `ap_pass`, se reinicia el AP (respuesta `wifi_restart=true`).
- Si cambia `mdns` y hay STA conectado, se reinicia mDNS.

Reset:
- `POST /api/config/reset` borra `dogrgb_cfg` y vuelve a defaults.
- Nota: el reset actual borra tambien `home_*`.

---

## UI (/config)

Campos:
- Brightness (1..255)
- Modo (Speed / Geofence / Show)
- 9 rangos de velocidad (km/h)
- Distancia maxima geofence (m)
- Efectos por rango (A/B, speed, intensity)
- AP SSID
- AP password (opcional)
- Checkbox "AP abierto (sin password)"
- mDNS
- Home: botones "Nuevo Home (GPS actual)" y "Clear Home"

Acciones:
- Guardar
- Restaurar defaults

Notas:
- Si cambias SSID/password/mDNS, el AP puede reiniciarse y desconectar la sesion.
- El firmware valida todo el JSON aunque el frontend haga validacion basica.
