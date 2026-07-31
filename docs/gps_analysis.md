# Análisis y Validación del Módulo GPS (Fase 1 MVP)

## 1. Resumen Ejecutivo
Se ha realizado una auditoría técnica profunda del código firmware (`main.cpp`) encargado de la lectura, parsing y procesamiento de datos GNSS (GPS). El objetivo fue verificar la precisión de los cálculos de velocidad y distancia, contrastar la implementación con estándares de la industria y detectar posibles mejoras.

**Conclusión General:** El código es **robusto, correcto y seguro** para la Fase 1. La lógica matemática es exacta. Se ha identificado una mejora menor recomendada para evitar la acumulación de "distancia fantasma" en reposo.

---

## 2. Validación de Lógica Existente

### 2.1 Parsing NMEA (Sentencia RMC)
*   **Estándar:** La sentencia `$GPRMC` ubica la velocidad en nudos en el **campo 7**.
*   **Implementación:** El código cuenta correctamente las comas y extrae el campo 7.
*   **Conversión:** Se utiliza el factor correcto: `1 nudo = 1.852 km/h`.
*   **Estado:** ✅ **CORRECTO** (Verificado contra documentación oficial NMEA 0183).

### 2.2 Cálculo de Distancia
*   **Método:** Fórmula de Haversine (distancia ortodrómica entre dos puntos en una esfera).
*   **Radio Terrestre:** Se usa `6371000.0` metros.
*   **Implementación:** Matemáticamente correcta usando `sin`, `cos`, `atan2`, `sqrt`.
*   **Estado:** ✅ **CORRECTO**.

### 2.3 Filtros de Seguridad (Sanity Checks)
*   **Picos de Velocidad:** Se descartan velocidades `> 40 km/h` (`SPEED_MAX_VALID_KPH`). Esto filtra errores de multipath severos.
*   **Saltos de Distancia:** Se descartan segmentos `> 50m` en 1 segundo (equiv. 180 km/h).
*   **Estado:** ✅ **CORRECTO** y buena práctica para rastreadores de mascotas.

---

## 3. Comparativa con Estándares de la Industria

| Característica | Implementación Actual (Dog-RGB) | Estándar Industria (Garmin/Tractive) | Evaluación |
| :--- | :--- | :--- | :--- |
| **Filtro de Ruido** | Umbral ("Clamping"): <br> - Ignora Vel > 40km/h <br> - Ignora Dist > 50m | **Filtro de Kalman:** <br> Modelo predictivo probabilístico. | El método actual es eficiente (baja CPU) y suficiente para un MVP. Kalman es superior en curvas suaves pero complejo. |
| **Micromovimientos** | Filtro de velocidad (`SPEED_ACTIVE_KPH` = 0.7 km/h) solo para *tiempo activo*. | **Gating con IMU:** <br> Acelerómetro confirma movimiento físico. | El estándar actual es adecuado para GPS-only. La fusión con IMU es el paso lógico para Fase 2. |
| **Acumulación** | Suma lineal de segmentos Haversine. | **Simplificación (Douglas-Peucker):** <br> Suaviza la ruta antes de sumar. | La suma lineal tiende a sobrestimar ligeramente por "jitter", pero es aceptable para uso recreativo. |

---

## 4. Hallazgo Específico: Filtro de Micromovimientos

El firmware actual aplica de forma consistente el filtro de "actividad" (0.7 km/h):

1.  **Tiempo Activo (`active_time_ms`):** solo suma un intervalo si las observaciones GNSS confiables de ambos extremos superan `SPEED_ACTIVE_KPH`.
2.  **Distancia Total (`total_distance_m`):** solo evalúa segmentos dentro de una muestra activa y además aplica el umbral dinámico basado en HDOP.
3.  **Bloqueos del loop:** el tiempo usa la marca UTC de cada RMC, no la hora en que el loop procesa la sentencia. Una cola de sentencias conservada durante un bloqueo mantiene sus intervalos reales.

Un intervalo aislado de más de tres segundos se rechaza porque no existe evidencia suficiente para afirmar que el perro estuvo activo durante todo el hueco. La siguiente observación establece una nueva línea base.

Si un intervalo cruza medianoche, la sesión recibe el intervalo completo y el contador diario nuevo recibe únicamente la fracción posterior a las 00:00. Ambos contadores saturan en su máximo representable en vez de desbordarse.

**Comportamiento actual simplificado:**
```cpp
if (previous_active && current_active && delta_ms <= GPS_ACTIVE_MAX_GAP_MS) {
    active_time_ms += delta_ms;
}
```

---

## 5. Conclusión
El módulo GPS está listo para operar. La lógica es segura y los cálculos son precisos. La mejora del filtro de distancia es recomendable para la experiencia de usuario (evitar falsos positivos de distancia), pero no crítica para el funcionamiento del sistema.
