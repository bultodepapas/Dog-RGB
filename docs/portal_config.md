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
  "version": 4,
  "mode": "speed",
  "fence_max_m": 300,
  "led": {
    "brightness": 77
  },
  "speed_ranges_kph": [2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0, 18.0],
  "effects": {
    "range1": {"a": 7, "b": 7, "speed": 40, "intensity": 80},
    "range2": {"a": 7, "b": 7, "speed": 58, "intensity": 95},
    "range3": {"a": 7, "b": 7, "speed": 76, "intensity": 110},
    "range4": {"a": 7, "b": 7, "speed": 94, "intensity": 125},
    "range5": {"a": 7, "b": 7, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 7, "speed": 130, "intensity": 155},
    "range7": {"a": 7, "b": 7, "speed": 148, "intensity": 170},
    "range8": {"a": 7, "b": 7, "speed": 166, "intensity": 180},
    "range9": {"a": 7, "b": 7, "speed": 184, "intensity": 190},
    "range10": {"a": 7, "b": 7, "speed": 200, "intensity": 200}
  },
  "single": {
    "effect": 0,
    "speed": 80,
    "intensity": 140,
    "rgb": {"r": 0, "g": 60, "b": 60}
  },
  "wifi": {
    "ap_ssid": "DogRGB",
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
  "version": 4,
  "mode": "speed",
  "fence_max_m": 300,
  "led": {"brightness": 77},
  "speed_ranges_kph": [2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0, 18.0],
  "effects": {
    "range1": {"a": 7, "b": 7, "speed": 40, "intensity": 80},
    "range2": {"a": 7, "b": 7, "speed": 58, "intensity": 95},
    "range3": {"a": 7, "b": 7, "speed": 76, "intensity": 110},
    "range4": {"a": 7, "b": 7, "speed": 94, "intensity": 125},
    "range5": {"a": 7, "b": 7, "speed": 112, "intensity": 140},
    "range6": {"a": 7, "b": 7, "speed": 130, "intensity": 155},
    "range7": {"a": 7, "b": 7, "speed": 148, "intensity": 170},
    "range8": {"a": 7, "b": 7, "speed": 166, "intensity": 180},
    "range9": {"a": 7, "b": 7, "speed": 184, "intensity": 190},
    "range10": {"a": 7, "b": 7, "speed": 200, "intensity": 200}
  },
  "single": {
    "effect": 0,
    "speed": 80,
    "intensity": 140,
    "rgb": {"r": 0, "g": 60, "b": 60}
  },
  "wifi": {
    "ap_ssid": "DogRGB",
    "ap_pass": "Dog12345",
    "ap_open": false,
    "mdns": "dog-collar"
  }
}
```

Notas:
- `ap_pass` es opcional. Si no se envia, se mantiene el valor actual.
- Si `ap_open` es `true`, el AP se guarda sin password.
- `mode`: `"speed"` (default), `"geofence"`, `"show"` o `"simple"`.
- `fence_max_m`: distancia maxima en metros (se divide en 10 rangos iguales).
- `mode: "show"` habilita demo de efectos en Segmento B (homogeneous puede pisar todo). El modo Show recorre los 12 efectos y usa color base aleatorio por efecto cuando el efecto lo permite; RAINBOW, GRADIENT_WAVE y FIRE no reflejan directamente ese color base.
- `mode: "simple"` aplica un solo efecto a toda la tira (incluye LEDs de estado).
- `single`: parametros del modo simple (effect/speed/intensity/rgb).

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
- `mode`: `speed` | `geofence` | `show` | `simple`
- `fence_max_m`: 50..5000
- `speed_ranges_kph`: 9 valores > 0, estrictamente ascendentes
- `effects`: `range1..range10` presentes
- `effect a/b`: 0..11
- `speed/intensity`: 0..255
- `single.effect`: 0..11
- `single.speed`: 0..255
- `single.intensity`: 0..255
- `single.rgb`: 0..255 por canal
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
- `single`
- `single values`
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
- `single_eff` (uint8)
- `single_speed` (uint8)
- `single_intensity` (uint8)
- `single_r` (uint8)
- `single_g` (uint8)
- `single_b` (uint8)
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
- Modo (Speed / Geofence / Show / Simple)
- Modo Simple (effect, speed, intensity, RGB, tema)
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
