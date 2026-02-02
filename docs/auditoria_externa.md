# Informe de Auditoría de Código - Dog-RGB Firmware

**Fecha:** 02 de Febrero de 2026
**Auditor:** Agente Senior de Ingeniería (AI)
**Repositorio:** Dog-RGB
**Componente Auditado:** Firmware ESP32-S3 (PlatformIO)

---

## 1. Resumen Ejecutivo

Se ha realizado una revisión estática del código fuente del firmware localizado en `Platformio/Dog-RGB`. El objetivo ha sido identificar vulnerabilidades de estabilidad, discrepancias con la documentación y oportunidades de mejora arquitectónica.

El firmware se encuentra en un estado funcional y bien estructurado, con una clara separación de responsabilidades. Sin embargo, se ha detectado **un fallo crítico de gestión de memoria** que representa un riesgo alto de reinicios inesperados (Crash/Panic) durante la operación de configuración vía web.

## 2. Alcance

La auditoría cubrió los siguientes archivos:
- `Platformio/Dog-RGB/src/main.cpp`: Lógica principal, servidor web, gestión GPS y LEDs.
- `Platformio/Dog-RGB/include/config.h`: Parámetros de configuración estática.
- `Platformio/Dog-RGB/platformio.ini`: Entorno de compilación.
- `docs/manual_de_colores.md`: Verificación de consistencia funcional.

---

## 3. Hallazgos

### 3.1. [CRÍTICO] Riesgo de Desbordamiento de Pila (Stack Overflow) en API JSON

**Gravedad:** ALTA 🔴
**Ubicación:** `src/main.cpp` : `handle_config_post()` y `handle_config_get()`

**Descripción:**
El código reserva buffers estáticos de gran tamaño (`StaticJsonDocument`) directamente en la memoria de pila (Stack) dentro de las funciones manejadoras del servidor web.

La tarea predeterminada de Arduino (`loopTask`) en ESP32 tiene un tamaño de pila limitado (usualmente 8KB). Reservar 4KB para una sola variable local consume instantáneamente el 50% de la pila disponible. Sumado al overhead del servidor web, las llamadas a funciones internas y el contexto del sistema operativo (FreeRTOS), esto crea una probabilidad muy alta de corrupción de memoria o excepciones de puntero (Guru Meditation Error).

**Evidencia (`src/main.cpp`):**
```cpp
// Línea 669
static void handle_config_post() {
  // ...
  // ¡PELIGRO! 4KB reservados en el Stack
  StaticJsonDocument<4096> doc; 
  const DeserializationError err = deserializeJson(doc, server.arg("plain"));
  // ...
}

// Línea 644
static void handle_config_get() {
  // ¡PELIGRO! 3KB reservados en el Stack
  StaticJsonDocument<3072> doc;
  // ...
}
```

**Recomendación:**
Reemplazar `StaticJsonDocument` por `DynamicJsonDocument` para asignar la memoria en el Heap (montón), o reducir drásticamente el tamaño del buffer si se confirma que los payloads reales son pequeños.

```cpp
// Solución sugerida:
DynamicJsonDocument doc(4096); // Se asigna en Heap, seguro para el Stack.
```

---

### 3.2. [MEDIO] Fragmentación de Memoria por Concatenación de Strings

**Gravedad:** MEDIA 🟠
**Ubicación:** `src/main.cpp` : `html_page()`, `html_config_page()`

**Descripción:**
Se utiliza la clase `String` de Arduino para construir respuestas HTML completas mediante concatenación sucesiva.

En aplicaciones de larga duración (como un collar que funciona todo el día), esto provoca fragmentación de la memoria Heap. Aunque el ESP32-S3 dispone de amplia RAM, la fragmentación excesiva puede llevar a fallos en la asignación de memoria para otras tareas críticas (como buffers de Wi-Fi o BLE) tras periodos prolongados de uso.

**Evidencia (`src/main.cpp`):**
```cpp
// Línea 497
static String html_page() {
  return String(
      "<!doctype html><html>..." 
      // Un solo String gigante creado en el Heap
      // ...
      );
}

// Línea 618
static String html_wifi_page() {
  String page = "<!doctype html>...";
  // Concatenación dinámica
  page += "<input name='ssid' value='" + wifi_ssid + "'>"; 
  return page;
}
```

**Recomendación:**
Utilizar envío por tramos (`server.sendContent()`) o almacenar las partes estáticas del HTML en memoria de programa (`PROGMEM`) usando tipos `const char[]`.

---

### 3.3. [BAJO] Bloqueo de Interrupciones durante Actualización de LEDs

**Gravedad:** BAJA 🟢
**Ubicación:** `src/main.cpp` : `strip_a.show()`

**Descripción:**
La librería `Adafruit_NeoPixel` deshabilita las interrupciones globales para garantizar el timing preciso del protocolo de los LEDs SK6812.

Mientras las interrupciones están desactivadas, la CPU no puede atender la UART del GPS. Aunque el ESP32 tiene buffers FIFO de hardware para la UART, si la cantidad de LEDs aumenta significativamente en el futuro, el tiempo de bloqueo podría exceder la capacidad del buffer FIFO, provocando pérdida de caracteres NMEA (`gps_overflow`).

**Cálculo de riesgo actual:**
- 24 LEDs * 32 bits/LED * 1.25 µs/bit ≈ 0.96 ms.
- A 9600 baudios, un byte llega cada ~1 ms.
- **Conclusión:** Con 24 LEDs, el margen es seguro. Si se aumentara a >100 LEDs, se perderían datos GPS.

**Recomendación:**
Mantener la cantidad de LEDs bajo control o migrar a una librería que use RMT o I2S (como `Esp32RMT` para NeoPixel) que no requiere bloquear la CPU.

---

### 3.4. [INFO] Consistencia Funcional

**Estado:** APROBADO ✅

Se ha verificado la lógica del código contra `docs/manual_de_colores.md`:
1. **Rangos de Velocidad:** Coinciden exactamente los valores en `config.h` (R1=2.0kph ... R9=34.0kph).
2. **Prioridad de UI:** El código respeta la prioridad: Error Crítico > Estado Wi-Fi > Estado AP > GPS OK.
3. **Persistencia:** La lógica de guardado en NVS (`save_metrics`, `save_config`) parece correcta y está protegida por intervalos de tiempo para evitar desgaste de la memoria Flash.

---

## 4. Conclusión Final

El firmware es apto para pruebas de campo, **siempre y cuando se corrija el hallazgo 3.1 (Stack Overflow)**. No se recomienda el despliegue a producción o uso intensivo de la interfaz de configuración web sin aplicar dicha corrección, ya que existe un riesgo real de "brick" temporal (reinicio constante) al guardar configuraciones.
