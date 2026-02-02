# Manual de Construccion - Smart LED Dog Collar (MVP)

[Espanol](manual_de_construccion.es.md) | [English](manual_de_construccion.en.md)

Guia completa para construir el prototipo con enfoque en buenas practicas de electronica.

---

## 1) Alcance y seguridad

- Cubre el MVP Fase 1: GPS + portal Wi-Fi + LEDs SK6812.
- Requiere conocimientos basicos de electronica y soldadura.
- No trabajes con la bateria conectada durante el armado.
- Verifica polaridad y continuidad antes de energizar.

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
- Opcion sin level shifter (prototipo): data directo con cables cortos
- Resistencias serie: 330-470 ohm (2 unidades, una por data line)
- Condensador: 1000 uF electrolitico, >=6.3V (1 unidad, cerca del primer LED)
- Condensador: 0.1 uF ceramico (1 unidad opcional, cerca del primer LED)
- Condensadores GNSS: 10-47 uF + 0.1 uF ceramico (1 set, cerca del modulo)

### Mecanica
- Correa de nylon (20-25 mm ancho)
- Tubo difusor de silicona
- Enclosure IP65-IP67 con alivio de tension
- Cables de silicona (AWG 22-24 para 5V/LEDs, AWG 26-28 para senales)

### Extras
- LED externo de estado + resistencia
- Interruptor fisico o sensor Hall (opcional)
- Termorretractil, cinta de doble cara, adhesivo epoxico

---

## 3) Herramientas recomendadas

- Cautin y esta?o
- Multimetro (continuidad y voltaje)
- Pinzas y cortadores
- Pistola de calor (termorretractil)
- Fuente de laboratorio (opcional, recomendado)

---

## 4) Diagrama de wiring (referencia)

Ver el diagrama en `docs/manual_de_uso.md`.
Pinout oficial XIAO ESP32-S3: `xiao_s3_pin.md`.

---

## 5) Preparacion

1) Corta correa y difusor a la longitud deseada.
2) Prepara cables (pelado corto, esta?o previo).
3) Etiqueta cables de 5V, GND y data.
4) Define la ruta de cables dentro del difusor y del enclosure.

---

## 6) Ensamble paso a paso

### Paso 1: Sistema de energia
1) Conecta bateria -> BMS -> cargador USB-C.
2) Conecta salida del BMS al boost 5V.
3) Verifica 5.0V estables con multimetro.
4) NO conectes aun las tiras LED ni el MCU.

### Paso 2: MCU y GNSS
1) Alimenta el XIAO ESP32-S3 a 3.3V.
2) Conecta GNSS:
   - GPS TX -> GPIO7 (D8) del MCU
   - GPS RX -> GPIO8 (D9) del MCU (opcional)
   - GND comun
   - VCC segun modulo (3.3V si aplica)
3) Coloca 10-47 uF + 0.1 uF entre VCC y GND del GNSS, lo mas cerca posible del modulo.
4) Si alimentas el GNSS desde el 3V3 del XIAO, verifica margen de corriente; ideal un LDO dedicado si hay ruido.
5) Mantener cables de GNSS cortos y lejos del boost y de los 5V de LEDs; evita bucles grandes.

### Paso 3: Level shifting y LEDs
1) Alimenta el 74AHCT125 con 5V y GND (si se usa).
2) Conecta data lines segun el numero de tiras:
   - Dos tiras:
     - GPIO11 -> IN1 -> OUT1 -> 330-470R (cerca de DIN) -> DIN LED A
     - GPIO12 -> IN2 -> OUT2 -> 330-470R (cerca de DIN) -> DIN LED B
   - Una sola tira:
     - GPIO11 -> IN1 -> OUT1 -> 330-470R (cerca de DIN) -> DIN LED A
     - No usar GPIO12
3) Si NO usas level shifter (prototipo): conecta GPIO11/GPIO12 directo a DIN con resistor serie de 330-470 ohm y cables cortos.
4) Conecta VDD y GND de las tiras a 5V y GND.
5) Coloca el condensador de 1000 uF en 5V cerca del primer LED (ideal uno por tira).
6) Lleva 5V y GND a cada tira en paralelo (no en serie); usa AWG 22-24 para 5V/GND.
7) Si hay caida de voltaje o tiras largas, inyecta 5V/GND al final de cada tira.

### Paso 4: LED de estado (opcional)
1) GPIO3 -> resistencia -> LED -> GND.

---

## 6.1) Notas sobre tiras de 4 cables

Las tiras SK6812 suelen tener 4 cables: +5V, GND, DIN y DOUT (o DI/DO).

- Para una sola tira: conecta +5V, GND y DIN. No necesitas DOUT.
- Para encadenar dos tiras en serie: conecta DOUT de la primera a DIN de la segunda.
- Para dos tiras separadas (este proyecto): cada tira va a su propio DIN (GPIO11 y GPIO12).

---

## 6.2) Opcion sin level shifter (mejor practica)

Si no usas level shifter, aplica estas reglas para maximizar estabilidad:

- Resistor serie de 330-470 ohm en cada data line, lo mas cerca posible del MCU.
- Cable de datos corto (ideal <10-15 cm).
- GND comun y retorno directo al mismo punto de alimentacion.
- Brillo bajo al inicio (30% o menos).
- Si es posible, reduce VDD de LEDs a ~4.0-4.5 V para mejorar compatibilidad con 3.3V.

---

## 6.3) Buenas practicas de cableado (GPS + LEDs)

- Usa punto estrella de GND en la salida del boost y desde ahi distribuye a MCU, GNSS y tiras.
- Mantener GNSS y su antena lejos del boost y cables de 5V/LEDs (min 3-5 cm si es posible).
- Si los cables son largos, torcer TX+GND del GNSS y evitar que corran paralelos a los 5V de LEDs.
- Para tiras largas o caida de voltaje, inyecta 5V/GND en ambos extremos y agrega 1000 uF por tira.
- Fija cables para evitar fatiga por flexion (alivio de tension interno).

---

## 7) Pruebas de banco (antes de cerrar)

1) Continuidad: confirma GND comun y sin cortos 5V-GND.
2) Energiza solo MCU y GNSS: confirma lectura GPS en serial.
3) Energiza LEDs con brillo bajo: confirma encendido de ambas tiras.
4) Verifica portal Wi-Fi (AP y STA) y ruta `/config`.
5) Para uso diario y configuracion, ve al Manual de uso: [docs/manual_de_uso.md](docs/manual_de_uso.md).

---

## 8) Descargar y flashear el ESP32

Este firmware usa PlatformIO.

1) Instala PlatformIO (VS Code o CLI).
2) Abre `firmware/esp32s3_base/` como proyecto.
3) Conecta el XIAO ESP32-S3 por USB.
4) Compila:
   - `pio run -e esp32s3`
5) Flashea:
   - `pio run -e esp32s3 -t upload`
6) Monitor serial:
   - `pio device monitor -e esp32s3`

Si el puerto no aparece, revisa el cable USB y drivers.

---

## 9) Ensamble final

1) Asegura conexiones con termorretractil.
2) Fija la tira LED dentro del difusor sin tension en cables.
3) Coloca el enclosure con alivio de tension.
4) Verifica que no queden puntos rigidos o cortantes.

---

## 10) Mantenimiento y ajustes

- Recarga la bateria con el cargador USB-C.
- Limpia con pano humedo (no sumergir si no es IP67).
- Ajusta pines en `firmware/esp32s3_base/include/pins.h` si cambias wiring.
- Ajusta parametros en `firmware/esp32s3_base/include/config.h` si cambias LEDs.

---

## 11) Errores comunes y prevencion

- LED sin encender: revisa 5V, GND comun y direccion DIN.
- GPS sin fix: prueba en exterior y revisa TX/RX cruzados.
- Reinicios del MCU: revisa caida de voltaje en 5V y GND.
- Flicker en LEDs: usa level shifter y resistor serie.
