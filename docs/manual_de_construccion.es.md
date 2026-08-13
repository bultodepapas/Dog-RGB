# Guía de Construcción de Dog-RGB

**Estado:** traducción de conveniencia, revisada el 2026-08-12. La versión canónica es la [guía en inglés](manual_de_construccion.en.md).

Esta guía resume el prototipo de dos tiras con XIAO ESP32-S3, GNSS EBYTE E108-GN02 y LEDs SK6812 RGBW. No reemplaza los datasheets ni certifica el collar como producto terminado.

## Seguridad

- Usa una celda Li-ion protegida, un cargador correcto para su química y una topología de protección/BMS entendida y documentada.
- No asumas que cargador y BMS pueden conectarse en cualquier orden.
- Nunca sueldes ni cambies cableado con la batería conectada.
- Empieza con una fuente de laboratorio limitada en corriente; conecta la batería únicamente después de validar lógica y LEDs.
- No cargues el collar puesto en el perro. Detén la prueba si hay calor, olor, deformación, caída de tensión o daño mecánico.
- Considera la resistencia al agua como no validada hasta probar el enclosure completo y todas sus entradas de cable.

## Configuración de referencia

| Elemento | Valor actual |
| --- | --- |
| MCU | Seeed Studio XIAO ESP32-S3 |
| GNSS | EBYTE E108-GN02, NMEA a 9.600 baud |
| LEDs | Dos tiras SK6812 RGBW de 24 píxeles |
| Datos LED | Tira A: D0/GPIO1; tira B: D1/GPIO2 |
| UART GNSS | TX del módulo a D7/GPIO44; RX del módulo desde D6/GPIO43 si se usa |
| Level shifter | 74AHCT125 o equivalente, recomendado |

Consulta el [pinout](../xiao_s3_pin.md), el [presupuesto de potencia](bom_power_budget.md) y la [guía SK6812](sk6812_wiring.md).

## Cableado lógico

```text
celda protegida -> carga/protección aprobada -> rieles regulados

D0 / GPIO1 -> level shifter -> 330–470 R -> DIN tira A
D1 / GPIO2 -> level shifter -> 330–470 R -> DIN tira B
GNSS TX     -> D7 / GPIO44
D6 / GPIO43 -> GNSS RX, si es necesario
GND         -> retorno común de baja impedancia
```

- Alimenta las tiras en paralelo y dimensiona cables/conectores usando corriente medida.
- Coloca cada resistencia cerca del `DIN` de su tira, condensadores bulk cerca de las entradas y desacoplo local en el buffer.
- Mantén antena y GNSS lejos del boost y de los lazos de corriente LED.
- Verifica el voltaje de alimentación de la variante exacta del GNSS.
- La señal directa a 3,3 V puede servir para diagnóstico con cable corto, pero no es la opción robusta final.

## Secuencia recomendada

1. Dibuja la topología de potencia y revisa polaridades.
2. Prueba únicamente los reguladores con fuente limitada en corriente.
3. Conecta XIAO y GNSS; flashea y confirma actividad GNSS al aire libre.
4. Añade una tira con brillo `77/255` o menor y mide tensión, corriente y temperatura.
5. Añade la segunda tira con rama de alimentación propia y repite las mediciones.
6. Ejecuta el efecto más brillante que pretendas usar durante una prueba sostenida.
7. Aísla conductores, añade alivio de tensión y cierra el enclosure solo después de aprobar todas las pruebas.

## Compilar y flashear

```powershell
cd Platformio/Dog-RGB
pio run -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor -e seeed_xiao_esp32s3
```

## Checklist antes de usar

- [ ] No hay cortos entre alimentación y GND.
- [ ] Las polaridades y conectores están identificados.
- [ ] Los rieles son estables durante arranque y brillo máximo previsto.
- [ ] Corriente pico, corriente estable y temperatura quedaron registradas.
- [ ] El GNSS obtiene fix confiable con los LEDs activos.
- [ ] Ambas tiras funcionan sin flicker ni reinicios.
- [ ] El portal, configuración, persistencia y exportación de ruta funcionan.
- [ ] El enclosure no tiene filos, conductores expuestos ni presión sobre la celda.
- [ ] El ajuste permite pasar cómodamente un dedo entre collar y cuello.

Lee la [guía de uso](manual_de_uso.md) para operación diaria. Ante cualquier diferencia técnica, sigue la guía inglesa y el código activo.
