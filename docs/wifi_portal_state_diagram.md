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
|  AP     |  | Conectar a STA     |
| mode    |  +---------+----------+
+----+----+            |
     |                 v
     |        +-------------------+
     |        | STA conectado     |
     |        +---------+---------+
     |                  |
     |        perdida   | ok
     |        de red    |
     |                  v
     |        +-------------------+
     |        | Reintento STA     |
     |        +---------+---------+
     |                  |
     |    falla > X s   |
     |                  v
     +-------------->+-------------------+
                     |  Fallback a AP   |
                     +-------------------+
```

Notas:
- AP por defecto cuando no hay credenciales.
- Fallback a AP si STA no conecta en X segundos.
- Boton fisico opcional para forzar AP.
```
