# Portal Web - Especificacion Funcional y Tecnica (Fase 1)

Este documento describe el portal web actual del collar (Fase 1). No incluye backend.

---

## 1. Objetivo

Crear un portal web sencillo que muestre datos basicos del collar y permita configuracion minima.

Metricas:
- Distancia recorrida (hoy)
- Velocidad promedio (hoy)
- Velocidad maxima (hoy)

---

## 2. Alcance Fase 1 (Actual)

Incluye:
- Dashboard unico con 3 metricas.
- Estado del collar (GPS OK / Sin GPS / Sin datos).
- Boton "Actualizar".
- Portal local via Wi-Fi (AP/STA).
- Configuracion runtime en `/config`.

No incluye:
- Selector de unidades.
- Mapas o historico.
- Cuentas, login, multi-usuario.
- Multi-collar.

---

## 3. Arquitectura General

- Collar -> Wi-Fi (AP o STA) -> Portal web local
- Sin backend en Fase 1

---

## 4. Flujo de Usuario

1) Usuario se conecta al AP del collar o a la misma red (STA).
2) Abre el portal (`/`).
3) Presiona "Actualizar" para leer datos.
4) Revisa el estado (GPS OK / Sin GPS / Sin datos).

---

## 5. Pantallas y UI

### 5.1 Dashboard Unico

Componentes:
- Header con nombre del collar.
- Cards principales:
  - Distancia total (km)
  - Velocidad promedio (km/h)
  - Velocidad maxima (km/h)
- Indicador de estado (GPS OK / Sin GPS / Sin datos).
- Ultima actualizacion (hora).

### 5.2 Estados

- Sin datos: mostrar "Sin datos".
- Sin GPS: mostrar "Sin GPS".
- Error: mostrar "Error".

---

## 6. Datos y Calculos

### Metricas
- Distancia total (hoy): sumatoria de segmentos validos.
- Velocidad promedio (hoy): distancia total / tiempo activo.
- Velocidad maxima (hoy): maximo de velocidades validas.

### Filtros
- Ignorar puntos sin fix.
- Descartar picos irreales (limite configurable).
- Umbral de movimiento para tiempo activo.

---

## 7. API local (Fase 1)

- `GET /api/summary`
  - distance_m
  - avg_speed_cmps
  - max_speed_cmps
  - last_update_min
  - gps_fix
  - has_data

---

## 8. Fase futura (BLE/App puente)

- Lectura BLE del resumen diario (ver `docs/ble_spec.md`).
- App puente opcional para sincronizar datos a un backend.

---

## 9. Requerimientos No Funcionales

- Tiempo de carga < 2 s.
- Vista responsive (movil y desktop).
- Accesibilidad basica (contraste, tamanos de texto).
