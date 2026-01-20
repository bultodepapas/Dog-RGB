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
     GND -----------------------+-------------------+------------------+---- GND (common)
                                |
XIAO ESP32-S3 (3.3V)             |
  GPIO11 (LED A data) -----------+--> 74AHCT125 IN1 -> OUT1 -> DIN A (330-470R)
  GPIO12 (LED B data) -----------+--> 74AHCT125 IN2 -> OUT2 -> DIN B (330-470R)
  GPIO7  (GPS RX / D6) <--------------------------- GPS TX
  GPIO8  (GPS TX / D7) ---------------------------> GPS RX (opcional)
  GPIO3  (Status LED) ----[R]----> LED externo -> GND
  3V3 ---------------------------> GPS VCC (si 3.3V)
  GND ----------------------------> GPS GND
```

Notas:
- Todos los GND deben ser comunes (MCU, GPS, LEDs, booster).
- Usa resistor serie de 330-470 ohm en cada data line.
- Decoupling recomendado: 1000 uF en 5V cerca del primer LED.
- En este proyecto: 2 tiras LED. Los primeros 3 LEDs de cada tira son de estado.

---

## 3) Estados LED (estado rapido)

Los primeros LEDs muestran estado del sistema:

- Azul pulsante: GPS buscando (sin fix).
- Azul fijo: GPS OK, sin Wi-Fi conectado.
- Verde fijo: Wi-Fi STA conectado.
- Amarillo fijo: AP activo sin intento STA.
- Rojo fijo: credenciales guardadas pero STA no conecta (fallback a AP).
- Rojo parpadeo rapido: error critico (sin GPS ni Wi-Fi por mucho tiempo).

Nota: durante intento STA con AP activo, el color puede seguir mostrando GPS
y no hay indicador exclusivo de "conectando".

---

## 4) Conectar al portal (modo AP)

1) Busca la red Wi-Fi del collar: `dog`
2) Password por defecto: `Dog123456789`
3) Abre el navegador:
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
- Cambiar rangos de velocidad (km/h).
- Cambiar efectos por rango.
- Cambiar SSID y mDNS del AP.
- Dejar el AP abierto con el checkbox "AP abierto (sin password)".

Acciones:
- "Guardar": aplica cambios en caliente.
- "Restaurar defaults": vuelve a valores de fabrica.

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
