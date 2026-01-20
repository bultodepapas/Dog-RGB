# Portal Wi-Fi (AP + STA) - Especificacion (Fase 1)

Este documento describe un portal web ultra simple que funciona en dos modos:
1) AP mode (Wi-Fi directo / hotspot del collar)
2) STA mode (Wi-Fi normal / router)

---

## Objetivo

- Mostrar 3 metricas basicas del collar:
  - Distancia recorrida (hoy)
  - Velocidad promedio (hoy)
  - Velocidad maxima (hoy)
- Configuracion minima de conectividad.

---

## Modo 1: AP (Wi-Fi Direct)

### Flujo de usuario
1) Collar crea red Wi-Fi: SSID "Dog-Collar".
2) Usuario se conecta desde el telefono.
3) Abre `http://192.168.4.1`.
4) Ve la pagina con las metricas y boton "Actualizar".

### Ventajas
- No requiere infraestructura externa.
- Funciona en cualquier lugar.

### Limitaciones
- El usuario debe cambiar de red Wi-Fi.
- No hay acceso a internet mientras esta conectado.

---

## Modo 2: STA (Wi-Fi Normal)

### Flujo de usuario (setup inicial)
1) Usuario entra al AP del collar.
2) Pagina de configuracion solicita SSID y password.
3) Collar guarda credenciales y se conecta al router.
4) Portal accesible por mDNS (ej: `http://dog-collar.local`).

### Ventajas
- Uso mas comodo despues del setup.
- Se puede acceder desde varios dispositivos en la misma red.

### Limitaciones
- Requiere Wi-Fi disponible.
- Setup inicial obligatorio.

---

## UI (Portal Web)

### Pagina principal
- Titulo: "Dog Collar"
- Estado: GPS OK / Sin GPS
- Cards:
  - Distancia (km)
  - Velocidad promedio (km/h)
  - Velocidad maxima (km/h)
- Boton: "Actualizar"
- Footer: "Ultima lectura: HH:MM"

### Pagina de configuracion Wi-Fi (solo si STA)
- Campo SSID
- Campo Password
- Boton "Guardar y conectar"
- Estado de conexion

---

## Endpoints minimos

- `GET /api/summary`
  - Devuelve JSON con distancia, avg, max, flags
- `GET /` pagina principal
- `POST /api/wifi` (solo STA)
  - Guarda SSID/password

---

## Datos (formato JSON ejemplo)

```
{
  "date": 20260120,
  "distance_m": 12400,
  "avg_speed_cmps": 480,
  "max_speed_cmps": 1820,
  "last_update_min": 1115,
  "gps_fix": true
}
```

---

## Estados y mensajes

- Sin GPS: "Esperando GPS"
- Sin datos: "Sin datos, intenta mas tarde"
- Modo AP: "Conectado al collar"
- Modo STA: "Conectado a Wi-Fi"

---

## Seguridad basica

- AP con password configurable (opcionalmente abierto).
- No exponer credenciales en el frontend.
- Limitar endpoints solo a la red local.

---

## Checklist MVP

- Portal carga en <2 s.
- AP mode funcional sin configuracion.
- STA mode con setup minimo.
- JSON correcto en `/api/summary`.
- UI legible en movil y desktop.
