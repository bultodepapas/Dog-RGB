# Portal Wi-Fi - Plan de Implementacion (Fase 1)

Este plan detalla la implementacion del portal web local (AP + STA) para el collar.

---

## 1. Objetivo tecnico

- Portal accesible localmente sin backend.
- AP mode por defecto, STA opcional.
- Pagina unica con 3 metricas y estado.

---

## 2. Flujo de arranque (modo)

Prioridad:
1) Si existen credenciales guardadas y la conexion es exitosa -> STA
2) Si falla -> AP
3) Si no hay credenciales -> AP

Transiciones:
- Usuario puede forzar modo AP con boton fisico (opcional).
- Si STA pierde red por mas de X segundos -> volver a AP.

---

## 3. Endpoints minimos

- `GET /` -> pagina principal
- `GET /api/summary` -> JSON con metricas
- `GET /wifi` -> pagina de configuracion Wi-Fi
- `POST /api/wifi` -> guardar SSID/password

---

## 4. JSON de resumen

Campos:
- date (yyyymmdd)
- distance_m
- avg_speed_cmps
- max_speed_cmps
- last_update_min
- gps_fix (bool)
- has_data (bool)

---

## 5. UI (pagina principal)

Componentes:
- Titulo: Dog Collar
- Estado: Conectado / Sin GPS / Sin datos
- Cards:
  - Distancia (km)
  - Velocidad promedio (km/h)
  - Velocidad maxima (km/h)
- Boton: Actualizar
- Footer: Ultima lectura HH:MM

Estados:
- Sin GPS -> mensaje en header
- Sin datos -> cards en "--"

---

## 6. UI (configuracion Wi-Fi)

Campos:
- SSID
- Password
- Boton: Guardar y conectar
- Estado: Conectando / Exito / Error

---

## 7. Persistencia

- Guardar credenciales Wi-Fi en NVS.
- Guardar ultimo modo activo.
- Al reiniciar, usar flujo de arranque.

---

## 8. Seguridad basica

- AP con password simple configurable.
- No exponer password en frontend.
- Limitar endpoints a LAN.

---

## 9. Pruebas

- AP mode: conectar y abrir pagina en <5 s.
- STA mode: guardar credenciales y reconectar.
- Cambio de red: fallback a AP.
- JSON valido en `/api/summary`.

---

## 10. Deliverables

- Portal funcional en AP.
- Portal funcional en STA.
- Pagina de configuracion.
- Documentacion de uso para usuario final.
- Diagrama de estados: `docs/wifi_portal_state_diagram.md`
