# Portal Wi-Fi - Diagrama de Estados (AP/STA/Fallback)

```
+----------------------+
|        Boot          |
+----------+-----------+
           |
           v
+----------------------+
| Hay credenciales?    |
+----+-----------+-----+
     |           |
    no          si
     |           |
     v           v
+---------+  +--------------------+
|  AP ON  |  | STA + AP ON        |
+----+----+  +---------+----------+
     |                 |
     |     STA ok?     | no
     |                 v
     |        +-------------------+
     |        |  AP fallback      |
     |        +-------------------+
     |
     v
+----------------------+
| Politica AP/Wi-Fi     |
+----------------------+
```

Politica AP/Wi-Fi (loop):
- Sin GPS fix: AP forzado ON.
- Con GPS OK y estacionario: AP ON.
- AP sin clientes por `AP_IDLE_TIMEOUT_MS`: AP OFF.
- Si AP OFF y no hay STA conectado: Wi-Fi OFF.
- Wi-Fi OFF se reactiva si vuelve a faltar GPS o se detecta estacionario.

Notas:
- STA se intenta cuando hay credenciales.
- Si STA falla en `STA_CONNECT_TIMEOUT_MS`, se queda en AP.
