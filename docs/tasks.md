# Tareas Pendientes (Plan Detallado)

Este documento lista todas las tareas pendientes para implementar la Fase 1 y preparar el camino a fases futuras.

## Fase 1 - MVP GPS-First (Sin Backend)

### Firmware (ESP32-S3 + GPS)
- Definir pines finales de GPS RX/TX y LED de estado en `include/pins.h`.
- Validar baudrate del GPS y ajustar si es necesario.
- Implementar parser NMEA robusto (RMC y/o VTG) para velocidad.
- Filtrado de picos GPS (l?mite de velocidad y salto de distancia).
- C?lculo de distancia total (Haversine entre puntos v?lidos).
- C?lculo de velocidad promedio (distancia / tiempo activo).
- C?lculo de velocidad m?xima con filtros.
- L?gica de ?tiempo activo? (umbral de velocidad + pausa).
- Manejo de cambio de d?a usando hora GPS.
- Persistencia de m?tricas diarias en NVS/flash con write throttling.
- Exponer bloque BLE con resumen diario (layout acordado).
- Estado de GPS fix y flags de datos disponibles.
- Logs b?sicos por Serial para diagn?stico.

### BLE (Perfil Simple)
- Definir UUIDs del servicio y caracter?stica.
- Especificar layout exacto del bloque de datos (byte layout + endian).
- Definir checksum simple (XOR) y validaci?n en app.
- Confirmar tama?o m?ximo de payload BLE.

### App M?vil (MVP)
- App con 1 pantalla (dashboard).
- Bot?n ?Sincronizar? (lectura BLE).
- Mostrar 3 m?tricas (distancia, promedio, m?xima).
- Mostrar estado: conectado/no conectado y GPS OK/no fix.
- Manejo de errores de conexi?n y reintentos.
- Conversi?n de unidades (km/h y km).
- Persistencia local de la ?ltima lectura.

### UX y Copys
- Redactar textos simples para estado (Esperando GPS, Ac?rcate al collar, etc.).
- Validar legibilidad para usuarios no t?cnicos.

### Validaci?n
- Prueba est?tica (distancia = 0).
- Prueba caminata corta con comparaci?n a app GPS del tel?fono.
- Prueba velocidad m?xima en trote/carrera.
- Verificaci?n de estabilidad BLE (conexi?n en <10 s).

---

## Fase 1.1 - Pulido

### Firmware
- Ajuste fino de umbrales (velocidad m?nima y tiempo de pausa).
- Protecci?n contra p?rdida temporal de GPS.
- Optimizaci?n de consumo (sleep ligero si no hay movimiento).

### App
- Selector simple de fecha ?Hoy/Ayer? (si hay datos guardados localmente).
- Export simple (captura o compartir). 

---

## Fase 2 - Motion (IMU)

### Hardware
- Selecci?n final IMU (BMI270 / ICM-42688) y ubicaci?n f?sica.
- Integraci?n de alimentaci?n y filtrado.

### Firmware
- Driver IMU y calibraci?n b?sica.
- Clasificaci?n de movimiento (bajo/medio/alto).
- Fusi?n de datos GPS + IMU.
- Refinar patrones de LED basados en actividad.

---

## Fase 3 - Heart Rate

### Hardware
- Selecci?n de sensor HR y montaje seguro.
- Aislamiento mec?nico para lectura estable.

### Firmware
- Driver HR + validaci?n de se?al.
- Definir rangos seguros y umbrales.
- Combinar HR con IMU y GPS para perfiles.

---

## Fase 4 - Miniaturizaci?n

### Mec?nico
- Dise?o de carcasa compacta.
- Reducci?n de peso total y tama?os de bater?a.

### El?ctrico
- Reducci?n de PCB y selecci?n de componentes SMD.
- Optimizar consumo y disipaci?n t?rmica.

---

## Infraestructura y Documentaci?n

- Documento BLE Spec (servicio + characteristic + layout de bytes).
- Documento de c?lculo GPS (distancia/velocidad) con f?rmulas y l?mites.
- Especificaci?n de pruebas con tolerancias aceptables.
- Lista de materiales MVP (BOM) con proveedores y costos.
- Esquema el?ctrico preliminar.
- Diagrama de bloques actualizado.

---

## Gesti?n y Operaci?n

- Definir qui?n implementa firmware vs app.
- Estimar tiempos por tarea.
- Lista de riesgos (GPS sin fix, BLE inestable, consumo alto).
- Plan de iteraci?n con pruebas r?pidas en campo.
