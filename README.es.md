# Dog-RGB

[English (principal)](README.md) · [Documentación](docs/README.md) · [Manual de uso](docs/manual_de_uso.md) · [Manual de construcción](docs/manual_de_construccion.es.md)

Dog-RGB es un collar inteligente DIY con tiras LED RGBW, GNSS y un XIAO ESP32-S3. Es un proyecto deliberadamente sobre-ingenierizado: la meta es explorar firmware, electrónica, telemetría y UX alrededor de una idea sencilla.

> **Estado:** prototipo funcional. El firmware y el portal están implementados y tienen pruebas automatizadas, pero la autonomía, temperatura, impermeabilización, comodidad y resistencia mecánica todavía deben validarse en el collar físico. No es hardware certificado para mascotas.

## Funciones actuales

- Métricas GNSS: distancia, tiempo activo, velocidad promedio y máxima.
- Historial local de ruta con exportación JSON, CSV y GeoJSON.
- Dos tiras SK6812 RGBW con layout semántico, mirror/orientación, 12 efectos, 8 paletas RGBW, 4 escenas integradas + 4 slots de usuario, Show por escenas, crossfades que preservan status, cuatro modos y un limitador global de corriente estimada.
- Modo Día opcional, que apaga los efectos entre 06:00 y 16:00 sin detener alertas ni rastreo.
- Portal Wi-Fi local AP/STA con portal cautivo, escaneo de redes, configuración (incluida calibración eléctrica LED opcional), API versionada de escenas/import/export y diagnóstico.
- PIN opcional para escrituras del portal y protección CSRF; la telemetría de lectura sigue accesible dentro de la red local.
- Persistencia robusta con registros A/B y CRC, incluido un banco independiente de escenas; partición NVS dedicada para rutas.
- Resumen BLE de 16 bytes implementado, pero desactivado por defecto por coexistencia de radio con SoftAP.

No están implementados: nube, cuentas, app móvil, IMU, ritmo cardíaco, telemetría de batería, OTA ni el editor gráfico de escenas del portal. El store y la API de escenas sí están implementados.

## Inicio rápido

```powershell
cd Platformio\Dog-RGB
pio run -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3 -t upload
pio device monitor -e seeed_xiao_esp32s3
```

Conecta el teléfono a `DogRGB` con la clave inicial `Dog12345` y abre `http://192.168.4.1/`. Cambia esa clave antes del uso habitual.

Para detalles actualizados usa la documentación principal en inglés:

- [Guía de documentación](docs/README.md)
- [Arquitectura](docs/architecture.md)
- [Referencia de API](docs/api-reference.md)
- [Configuración runtime](docs/portal_config.md)
- [Pruebas y Wokwi](docs/testing.md)
- [Hardware y cableado](docs/manual_de_construccion.en.md)

Los planes y auditorías fechados se conservan como historial; no representan automáticamente el estado actual.

## Agradecimientos

Dog-RGB se desarrolla con el apoyo de [Codex for Open Source](https://developers.openai.com/community/codex-for-oss). Un agradecimiento especial a **OpenAI** y al **equipo de Codex** por proporcionar acceso a **ChatGPT Pro con Codex** para apoyar a quienes mantienen proyectos de código abierto.

Dog-RGB es un proyecto comunitario independiente y no está afiliado ni respaldado oficialmente por OpenAI.

## Licencia

Salvo que un archivo indique lo contrario, el código, las herramientas y la documentación originales de Dog-RGB se distribuyen bajo la licencia permisiva [MIT](LICENSE). El material de terceros conserva sus propias condiciones y licencias.
