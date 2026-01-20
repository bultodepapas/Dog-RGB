# Manual de Uso - Smart LED Dog Collar (MVP)

Este manual explica como encender, conectar y leer el collar en la Fase 1 (GPS + Wi-Fi portal).

---

## 1) Encendido

1) Enciende el collar.
2) Espera el arranque (1-2 s).
3) Observa la tira LED:
   - Azul pulsante: GPS buscando.
   - Azul fijo: GPS OK.
   - Amarillo fijo: Wi-Fi AP activo.
   - Verde fijo: Wi-Fi STA conectado.
   - Rojo fijo o parpadeo: error.

---

## 2) Conectar al portal Wi-Fi (AP)

1) Busca la red Wi-Fi: `dog`
2) Contrase?a: `Dog123456789`
3) Abre el navegador y entra a:
   - `http://192.168.4.1`
4) Veras el dashboard con:
   - Distancia
   - Velocidad promedio
   - Velocidad maxima

---

## 3) Configurar Wi-Fi normal (STA)

1) En el portal, entra a `Configurar Wi-Fi`.
2) Escribe el SSID y password de tu red.
3) Guarda.
4) El collar intentara conectarse.
5) Si conecta, abre:
   - `http://dog-collar.local`

---

## 4) Lectura de datos

- Presiona "Actualizar" para leer la ultima medicion.
- Si no hay datos:
  - Espera GPS (azul pulsante).
  - Intenta de nuevo.

---

## 4.1) Configuracion avanzada

- Abre `http://192.168.4.1/config` (AP) o `http://dog-collar.local/config` (STA).
- Ajusta brillo, rangos y efectos por velocidad.
- Presiona "Guardar" para aplicar cambios.
- Presiona "Restaurar defaults" para volver a valores de fabrica.

---

## 5) Estados LED (resumen)

- Azul pulsante: GPS buscando.
- Azul fijo: GPS OK.
- Verde fijo: Wi-Fi conectado.
- Verde pulsante: intentando Wi-Fi.
- Amarillo fijo: AP activo.
- Rojo fijo: error Wi-Fi.
- Rojo rapido: error critico.

---

## 6) Consejos

- Usa el collar al aire libre para mejor GPS.
- Mantente cerca del collar para acceso Wi-Fi.
- Si STA falla, el collar vuelve a AP automaticamente.

---

## 7) Soporte rapido

- Reinicia el collar si el portal no responde.
- Verifica bateria si no enciende LEDs.
