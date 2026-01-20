# Portal Web - Especificacion Funcional y Tecnica (Fase 1)

Este documento describe como debe funcionar el portal web para configuracion y datos del collar. Es una guia detallada para producto, UX y desarrollo. No incluye codigo.

---

## 1. Objetivo

Crear un portal web sencillo que muestre datos basicos del collar y permita configuracion minima. En Fase 1 se enfoca en:

- Distancia recorrida (hoy)
- Velocidad promedio (hoy)
- Velocidad maxima (hoy)

---

## 2. Alcance Fase 1 (MVP)

Incluye:
- Dashboard unico con 3 metricas.
- Estado del collar (conectado, GPS OK, bateria si esta disponible).
- Selector de unidades (km/h, mph).
- Fecha fija: hoy (sin historico).
- Sin mapas, sin rutas, sin exportacion.

No incluye:
- Cuentas, login, multi-usuario.
- Multi-collar.
- Historial ni reportes.
- Geocercas ni alertas.

---

## 3. Arquitectura General

Opciones posibles (se elige una para implementar):

### Opcion A (Recomendada para MVP)
- Collar -> BLE -> App puente (movil/desktop) -> API -> Web
- Web lee datos desde backend.

### Opcion B (Web Bluetooth directo)
- Collar -> BLE -> Navegador (Web Bluetooth)
- Sin backend. Limitado a navegadores compatibles.

Notas:
- El ESP32 no provee portal web en Fase 1.
- Fase 1 prioriza simplicidad y accesibilidad para usuarios normales.

---

## 4. Flujo de Usuario

1) Usuario abre el portal.
2) Portal muestra estado (ultimo sync).
3) Usuario sincroniza desde la app puente.
4) Portal actualiza metricas.

En caso de datos desactualizados:
- Mostrar mensaje "Sincroniza el collar para ver datos recientes".

---

## 5. Pantallas y UI

### 5.1 Dashboard Unico

Componentes:
- Header con nombre del collar.
- Cards principales:
  - Distancia total (km)
  - Velocidad promedio (km/h)
  - Velocidad maxima (km/h)
- Indicador de estado:
  - Conectado / Desconectado
  - GPS OK / Sin GPS
- Selector de unidades (km/h, mph).
- Ultima actualizacion (hora).

### 5.2 Estados

- Sin datos: "Sincroniza el collar".
- Sin GPS: "Esperando GPS".
- Datos invalidos: "Datos no validos, reintenta".

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

## 7. API (si se usa backend)

### Endpoints minimos
- POST /sessions (subir sesion agregada o cruda)
- GET /metrics/daily?device_id=...&date=YYYY-MM-DD
- GET /devices/:id/status

### Respuesta de ejemplo
- distance_m
- avg_speed_cmps
- max_speed_cmps
- last_update
- gps_fix
- battery

---

## 8. BLE (si se usa app puente)

- Servicio BLE: "Dog Collar Data"
- Caracteristica: DAILY_SUMMARY
- Payload: bloque fijo con fecha, distancia, velocidad promedio, velocidad maxima, flags y checksum.

---

## 9. UX y Copy

Texto simple y directo:
- "Sincroniza el collar"
- "Ultima lectura: HH:MM"
- "Esperando GPS"
- "Acercate al collar"

---

## 10. Requerimientos No Funcionales

- Tiempo de carga < 2 s.
- Vista responsive (movil y desktop).
- Accesibilidad basica (contraste, tamanos de texto).
- Persistencia local del ultimo estado si no hay red.

---

## 11. Validacion

- Metricas correctas comparadas con app GPS.
- Estado BLE refleja conexion real.
- Portal carga correctamente con datos vacios.
- UI clara sin explicaciones extras.

---

## 12. Roadmap

Fase 2:
- Historico 7/30 dias.
- Mapa de ruta.
- Export CSV.
- Multi-collar.

Fase 3:
- Alertas, geocercas.
- Integracion HR y IMU.
- Perfiles personalizados.
