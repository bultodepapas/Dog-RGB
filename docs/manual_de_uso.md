# Manual de Uso - Smart LED Dog Collar (MVP)

Guia practica para instalar, usar y configurar el collar en Fase 1 (GPS + portal Wi-Fi).

---

## 1) Requisitos y seguridad

- Telefono o laptop con Wi-Fi.
- Uso al aire libre para GPS confiable.
- Evita que el collar quede muy apretado; debe poder pasar un dedo.
- No sumerjas el collar si el enclosure no es IP67.

---

## 2) Instalacion fisica

1) Coloca el collar en el cuello del perro.
2) Ajusta hasta que quede firme pero comodo.
3) Verifica que el difusor LED no quede en contacto directo con piel sensible.
4) Enciende el collar.

---

## 2.1) Diagrama de wiring (XIAO ESP32-S3 + componentes)

```
  21700 Li-ion
      |
   BMS + USB-C
      |
  5V Boost (>=3A) --------------+-------------------+------------------+
      |                         |                   |                  |
     5V                      74AHCT125           SK6812 Strip A     SK6812 Strip B
      |                     (level shifter)         VDD/GND           VDD/GND
      |                         |
     GND ----+------------------+-------------------+------------------+---- GND (star)
            |
        3V3 Reg (si aplica) ----+-------------------+
            |                    |                  |
        XIAO ESP32-S3           GNSS (GPS)      10-47uF + 0.1uF

XIAO ESP32-S3 (3.3V)
  D0 / GPIO1 (LED A data) -------+--> 74AHCT125 IN1 -> OUT1 -> DIN A (330-470R)
  D1 / GPIO2 (LED B data) -------+--> 74AHCT125 IN2 -> OUT2 -> DIN B (330-470R)
  GPIO44 (GPS RX / D7) <--------------------------- GPS TX
  GPIO43 (GPS TX / D6) ---------------------------> GPS RX (opcional)
  GPIO3  (Status LED) ----[R]----> LED externo -> GND
  3V3 ---------------------------> GPS VCC (si 3.3V)
  GND ----------------------------> GPS GND
```

Notas:
- Todos los GND deben ser comunes (MCU, GPS, LEDs, booster) con punto estrella en la salida del boost.
- Usa resistor serie de 330-470 ohm en cada data line, cerca de DIN.
- Decoupling recomendado: 1000 uF en 5V cerca del primer LED (ideal uno por tira).
- GNSS: 10-47 uF + 0.1 uF cerca de VCC, cables cortos y lejos del boost/5V de LEDs.
- En este proyecto: 2 tiras LED. Los primeros 2 LEDs de cada tira son de estado.

---

## 3) Estados LED (estado rapido)

Los primeros LEDs muestran estado del sistema:

LED0 = Wi-Fi/AP
- Verde fijo: STA conectado.
- Verde pulsante: STA intentando conectar.
- Amarillo fijo: AP activo sin clientes.
- Amarillo pulsante: AP activo con clientes.
- Rojo fijo: STA fallo (fallback a AP).
- Ambar doble pulso: Wi-Fi apagado por ahorro.

LED1 = GPS
- Azul fijo: GPS OK.
- Azul pulsante: GPS buscando (sin fix).

Override:
- Rojo parpadeo rapido: sin GPS ni STA por >10 min.

Segmento resto:
- Si no hay GPS fix, muestra rainbow animado.
- Con GPS OK, usa efectos por rangos de velocidad.
- Si Wi-Fi esta OFF y GPS OK por >5 min, los LEDs de estado se igualan al segmento resto.

Modos especiales:
- Show: demo visual que recorre efectos automaticamente.
- Simple: un solo efecto configurado por el usuario, aplicado a toda la tira (incluye LEDs de estado).

AP/Wi-Fi auto:
- Si no hay GPS fix, el AP se mantiene encendido.
- Si la velocidad es <= 2 km/h por ~2 min, el AP se enciende automaticamente.
- Al arrancar o reiniciar el AP, queda visible al menos 15 min.
- La actividad del portal mantiene el AP activo por 5 min adicionales.
- Si no hay clientes ni actividad del portal por 10 min, el AP se apaga.
- Si no hay STA y no hay AP, el Wi-Fi se apaga para ahorrar bateria.

---

## 4) Conectar al portal (modo AP)

1) Busca la red Wi-Fi del collar: `dog`
2) Password por defecto: `Dog123456789`
3) El telefono puede mostrar automaticamente el portal cautivo. Si no aparece, abre el navegador:
   - `http://192.168.4.1`
4) Veras el dashboard con:
   - Distancia
   - Velocidad promedio
   - Velocidad maxima

Si el AP esta abierto, conectate sin password.

---

## 5) Configurar Wi-Fi normal (modo STA)

1) En el portal, entra a `Configurar Wi-Fi`.
2) Escribe el SSID y password de tu red.
3) Guarda.
4) El collar intenta conectarse.
5) Si conecta, abre:
   - `http://dog-collar.local`

El AP puede apagarse automaticamente al conectar en STA para ahorrar energia.

---

## 6) Configuracion avanzada (/config)

Acceso:
- AP: `http://192.168.4.1/config`
- STA: `http://dog-collar.local/config`

Desde aqui puedes:
- Ajustar brillo (1..255).
- Cambiar modo (Speed / Geofence / Show / Simple).
- Cambiar rangos de velocidad (km/h).
- Cambiar efectos por rango (IDs 0..11).
- Configurar el modo Simple (efecto, speed, intensity, RGB y tema).
- Cambiar SSID/AP password o dejar el AP abierto.
- Cambiar mDNS.

Acciones:
- "Guardar": aplica cambios en caliente.
- "Restaurar defaults": vuelve a valores de fabrica.

Nota: si cambias SSID/password del AP, el AP se reinicia y puede desconectar la sesion.
Nota: RAINBOW, GRADIENT_WAVE y FIRE ignoran el color base en modo Simple.

---

## 7) Lectura de datos

- Presiona "Actualizar" para leer la ultima medicion.
- Si no hay datos:
  - Espera a que el GPS tenga fix (azul pulsante -> azul fijo).
  - Intenta de nuevo.

---

## 8) Consejos de uso

- Para GPS rapido, espera 30-90 s en cielo abierto.
- Mantente cerca del collar para el portal Wi-Fi.
- Si no usas STA, puedes dejar solo AP.
- Si el portal no responde, reinicia el collar.

---

## 9) Solucion rapida de problemas

- No aparece el AP: revisa bateria y reinicia.
- STA no conecta: revisa SSID/password y vuelve a AP.
- LED rojo fijo: STA fallo; abre `192.168.4.1` y corrige credenciales.
- Sin datos GPS: prueba en exterior y espera.
