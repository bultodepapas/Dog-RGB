# App Puente MVP - Especificacion (Fase futura)

Esta especificacion define la app puente minimalista para leer el resumen diario del collar via BLE y mostrar 3 metricas. Se implementara en una fase posterior.

---

## Objetivo

- Conectar por BLE al collar.
- Leer el bloque de 16 bytes.
- Mostrar distancia, velocidad promedio y velocidad maxima.

---

## Alcance

Incluye:
- 1 pantalla (dashboard).
- 1 boton de sincronizacion.
- Estados basicos de conexion.
- Persistencia local de la ultima lectura.

No incluye:
- Login o cuentas.
- Multi-dispositivo.
- Historico o mapas.

---

## Flujo de Usuario

1) Abrir app.
2) Tocar "Sincronizar".
3) Conectar por BLE.
4) Leer bloque de datos.
5) Mostrar metricas.

---

## UX (pantalla unica)

Componentes:
- Titulo: "Dog Collar"
- Estado: Conectado/Desconectado, GPS OK/Sin GPS
- Boton: "Sincronizar"
- Cards:
  - Distancia (km)
  - Velocidad promedio (km/h)
  - Velocidad maxima (km/h)
- Footer: "Ultima lectura: HH:MM"

---

## Estados

- Sin conexion: "Acercate al collar"
- Sin GPS: "Esperando GPS"
- Datos invalidos: "Datos no validos, reintenta"

---

## BLE

- Escaneo por nombre "Dog-Collar" o UUID del servicio.
- Conectar y leer characteristic READ.
- Validar checksum XOR.
- Decodificar payload (ver `docs/ble_spec.md`).

---

## Conversiones

- distance_m -> km = m / 1000
- speed_cmps -> km/h = cmps * 0.036

---

## Persistencia Local

Guardar:
- ultima lectura decodificada
- timestamp local de lectura

---

## Errores y Reintentos

- Timeout conexion: 10 s
- 1 reintento automatico
- Mensajes simples para el usuario

---

## Checklist de entrega

- Conecta y lee en <10 s
- Checksum validado
- UI muestra datos coherentes
- Estados claros sin soporte tecnico
