# Manual de Construccion - Smart LED Dog Collar (MVP)

[Espanol](manual_de_construccion.es.md) | [English](manual_de_construccion.en.md)

Guia practica para construir el prototipo (hardware) del collar.

---

## 1) Alcance y advertencias

- Este manual cubre el MVP de Fase 1 (GPS + Wi-Fi portal + LEDs SK6812).
- Requiere conocimientos basicos de electronica y soldadura.
- Prueba todo primero en banco antes de sellar el enclosure.

---

## 2) Lista de materiales (BOM base)

### Electronica principal
- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz, BDS/GPS/GLONASS)
- LEDs: SK6812 (5V, single-wire), 2 tiras (20 LEDs cada una)
- Bateria: 21700 Li-ion (~5000 mAh)
- BMS 1S + cargador USB-C
- Boost 5V >= 3A continuo
- Regulador 3.3V (si no lo incluye el MCU)

### Nivel logico y proteccion
- Level shifter 5V: 74AHCT125 (o equivalente)
- Resistencia serie: 330-470 ohm (2 unidades, una por cada data line)
- Condensador: 1000 uF electrolitico (5V rail, cerca del primer LED)
- Condensador: 0.1 uF ceramico (cerca del primer LED, opcional)

### Mecanica
- Correa de nylon (20-25 mm ancho)
- Tubo difusor de silicona
- Enclosure IP65-IP67 con alivio de tension
- Cables de silicona (AWG 22-24 para 5V/LEDs, AWG 26-28 para senales)

### Extras
- LED externo de estado + resistencia (si se usa)
- Interruptor fisico o sensor Hall (opcional)
- Termorretractil, cinta de doble cara, adhesivo epoxico

---

## 3) Herramientas

- Cautin y esta?o
- Multimetro
- Pinzas y cortadores
- Pistola de calor (termorretractil)
- Fuente de laboratorio (opcional, recomendado)

---

## 4) Diagrama de wiring (referencia)

Ver el diagrama en `docs/manual_de_uso.md`.

---

## 5) Preparacion

1) Corta la correa y el tubo difusor a la longitud requerida.
2) Prepara los cables de alimentacion y datos.
3) Identifica y etiqueta pines y cables para evitar errores.

---

## 6) Ensamble paso a paso

### Paso 1: Power
1) Conecta la bateria al BMS.
2) Conecta el BMS al cargador USB-C.
3) Conecta el boost 5V a la salida del BMS.
4) Verifica con multimetro que el boost entrega 5.0V.

### Paso 2: MCU y GNSS
1) Alimenta el XIAO ESP32-S3 (3.3V).
2) Conecta GNSS:
   - GPS TX -> GPIO7 (D6) del MCU
   - GPS RX -> GPIO8 (D7) del MCU (opcional)
   - GND com?n
   - VCC segun modulo (3.3V si aplica)

### Paso 3: Level shifting y LEDs
1) Alimenta el 74AHCT125 con 5V y GND.
2) Conecta data lines:
   - GPIO11 -> IN1 -> OUT1 -> 330-470R -> DIN LED A
   - GPIO12 -> IN2 -> OUT2 -> 330-470R -> DIN LED B
3) Conecta VDD y GND de las tiras a 5V y GND.
4) Coloca el condensador de 1000 uF en 5V cerca del primer LED.

### Paso 4: LED de estado (opcional)
1) GPIO3 -> resistencia -> LED -> GND.

---

## 7) Pruebas basicas antes de cerrar

1) Verifica continuidad y ausencia de cortos.
2) Enciende y confirma que el MCU inicia.
3) Verifica que el GPS responde y obtiene fix.
4) Verifica que los LEDs encienden y cambian segun velocidad.
5) Verifica el portal Wi-Fi (AP y STA).

---

## 8) Cierre y ensamblaje final

1) Asegura conexiones con termorretractil.
2) Coloca el enclosure con alivio de tension.
3) Fija la tira LED dentro del difusor.
4) Verifica que no haya puntos rigidos o cortantes.

---

## 9) Mantenimiento

- Recarga la bateria con el cargador USB-C.
- Limpia el collar con un pano humedo.
- Revisa cables y conectores cada cierto tiempo.

---

## 10) Notas finales

- Ajusta pines en `firmware/esp32s3_base/include/pins.h` si cambias wiring.
- Ajusta parametros en `firmware/esp32s3_base/include/config.h` si cambias el numero de LEDs.
