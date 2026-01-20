# BOM + Power Budget (Fase 1 MVP)

Este documento resume los componentes base y el presupuesto de potencia para la Fase 1.

---

## BOM (Base)

### Control
- MCU: Seeed Studio XIAO ESP32-S3
- GNSS: EBYTE E108-GN02 (10 Hz)

### LEDs
- SK6812, 5V, single-wire
- 1 o 2 tiras
- LEDs por tira: 20 (min 10, max 50)

### Potencia
- Bateria: 21700 Li-ion ~5000 mAh
- Cargador USB-C (1S Li-ion)
- BMS 1S
- Boost 5V (>=2A continuo)
- Regulador 3.3V para MCU/GNSS

### Proteccion y pasivos
- Capacitor 1000 uF en entrada LED
- Resistor serie 330-470 ohm en DIN
- Cables flexibles + conector

---

## Power Budget (Estimado)

### LED (por tira)
- 20 LEDs @ 30% brillo (promedio)
- Corriente por LED aprox: 3-5 mA (idle)
- Total por tira: 60-100 mA

Dos tiras:
- Total LEDs: 40
- Corriente estimada: 120-200 mA

### MCU + GNSS
- ESP32-S3 activo: 80-150 mA (picos mayores)
- GNSS: 20-35 mA

### Total estimado (promedio)
- 1 tira: 150-250 mA
- 2 tiras: 250-400 mA

---

## Autonomia (21700 5000 mAh)

- 1 tira: ~12-20 h
- 2 tiras: ~8-14 h

*Valores aproximados, dependen de animaciones y brillo real.*

---

## Notas

- Ajustar el boost a 5V estable con buena eficiencia.
- El consumo maximo en blanco total debe considerarse para picos.
- Limitar brillo a ~30% protege bateria y calor.
