# Flujo GPS -> Calculo -> BLE

```
[GPS Fix]
   |
   v
[Leer NMEA: lat/lon/speed]
   |
   v
[Filtrar picos y datos invalidos]
   |
   +--> (sin fix) -> [No actualizar]
   |
   v
[Calcular distancia incremental]
   |
   v
[Actualizar tiempo activo]
   |
   v
[Actualizar velocidad maxima]
   |
   v
[Calcular velocidad promedio]
   |
   v
[Guardar resumen diario en memoria]
   |
   v
[Exponer resumen por BLE]
```

---

# Pantalla MVP (Wireframe Textual)

```
+-----------------------------+
| Dog Collar                  |
| Status: Conectado | GPS OK  |
|                             |
| [ Sincronizar ]             |
|                             |
| Distancia (hoy)             |
| 12.4 km                     |
|                             |
| Velocidad promedio          |
| 4.8 km/h                    |
|                             |
| Velocidad maxima            |
| 18.2 km/h                   |
|                             |
| Ultima lectura: 18:35       |
+-----------------------------+
```

Estados alternos:
- Sin conexion: "Acercate al collar"
- Sin GPS: "Esperando GPS"

---

# Diagrama de estados BLE (Conectado/Desconectado)

```
            +--------------------+
            |   Desconectado     |
            | (Idle/Advertising) |
            +----------+---------+
                       |
                       | Scan + Connect
                       v
            +--------------------+
            |     Conectado      |
            |  (Ready to Read)   |
            +----+----+-----+----+
                 |    |     |
         Read OK |    |     | Timeout/Error
                 |    |     v
                 |    |  +-----------------+
                 |    |  | Error/Retry     |
                 |    |  | (Retry/Backoff) |
                 |    |  +--------+--------+
                 |    |           |
                 |    |           | Give up
                 |    |           v
                 |    |    +-----------------------------+
                 |    |    |        Desconectado         |
                 |    |    |    (Idle/Advertising)       |
                 |    |    +-----------------------------+
                 |    |
                 |    v
                 |  +--------------------+
                 |  |  Sin GPS Fix       |
                 |  | (Mostrar aviso)    |
                 |  +---------+----------+
                 |            |
                 |  Reintentar lectura
                 |            v
                 |     +----------------+
                 |     |  Datos leidos  |
                 |     | (Summary OK)   |
                 |     +--------+-------+
                 |              |
                 |              | User disconnect
                 v              v
     +----------------+   +-----------------------------+
     | Datos invalidos|   |        Desconectado         |
     | (Checksum fail)|   |    (Idle/Advertising)       |
     +--------+-------+   +-----------------------------+
              |
              | Retry read
              v
     +----------------+
     |  Datos leidos  |
     | (Summary OK)   |
     +--------+-------+
              |
              | User disconnect
              v
            +-----------------------------+
            |        Desconectado         |
            |    (Idle/Advertising)       |
            +-----------------------------+
```
