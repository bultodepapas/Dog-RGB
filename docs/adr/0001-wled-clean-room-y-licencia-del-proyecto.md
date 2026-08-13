# ADR-0001: Aprendizaje clean-room de WLED mientras se define la licencia

**Estado:** Aceptado como política provisional de ingeniería

**Fecha:** 2026-08-12

**Alcance:** Firmware, portal, assets, documentación y futuras contribuciones de RGB Dog.

## Contexto

RGB Dog está estudiando patrones de [WLED v16.0.1](https://github.com/wled/WLED/tree/v16.0.1), cuyo código se publica bajo [EUPL-1.2](https://github.com/wled/WLED/blob/v16.0.1/LICENSE). En la fecha de esta decisión no hay un archivo `LICENSE` o `COPYING` en la raíz de RGB Dog que establezca cómo se puede usar, modificar o redistribuir este proyecto.

El análisis técnico recomienda adaptar conceptos de WLED —límite de corriente, registro y metadatos de efectos, segmentos semánticos, paletas, transiciones, presets y empaquetado web—, pero RGB Dog tiene un dominio, arquitectura y restricciones diferentes.

Elegir la licencia definitiva del proyecto corresponde al propietario y puede requerir revisión legal. No es necesario resolver esa elección para continuar diseñando desde principios y comportamiento observable.

## Decisión

Hasta que exista una licencia explícita de RGB Dog y se revise la compatibilidad de cualquier dependencia nueva:

1. Las ideas de WLED se implementarán **clean-room**, usando documentación pública, comportamiento observable y diseño propio.
2. No se copiarán literalmente código fuente, tablas de efectos/paletas, HTML, CSS, JavaScript, imágenes, iconos ni otros assets de WLED.
3. Los nombres genéricos de conceptos —efecto, paleta, segmento, preset, transición o límite de corriente— no implican reutilización de una implementación.
4. Cada cambio inspirado por WLED deberá tener pruebas propias y encajar en las APIs, persistencia y restricciones de RGB Dog.
5. Si más adelante se propone reutilizar material literal, el cambio deberá incluir:
   - archivo y commit/tag de origen;
   - licencia y copyright aplicables;
   - análisis de compatibilidad con la licencia elegida para RGB Dog;
   - atribuciones y avisos requeridos;
   - aprobación explícita del propietario antes de integrar.
6. Ningún binario o paquete se publicará afirmando una licencia de RGB Dog hasta que exista una decisión explícita y un archivo de licencia en raíz.

Esta ADR no selecciona una licencia definitiva ni ofrece una conclusión legal sobre compatibilidad con EUPL-1.2.

## Consecuencias

### Positivas

- Permite avanzar con las mejoras de arquitectura sin contaminar la procedencia del código.
- Conserva libertad para elegir después una licencia apropiada para el proyecto.
- Evita importar la complejidad estructural y compatibilidad histórica de WLED.
- Deja una regla verificable para contribuciones futuras.

### Costos

- Algunas funciones requerirán más diseño y pruebas que un port directo.
- No se podrán importar rápidamente efectos o assets de WLED.
- La distribución pública sigue teniendo una decisión pendiente.

## Criterio para reemplazar esta ADR

Esta política provisional puede actualizarse cuando:

1. el propietario elija y añada la licencia de RGB Dog;
2. exista un inventario de dependencias y material de terceros;
3. se documente la política de atribución y contribuciones;
4. cualquier reutilización literal propuesta haya sido revisada por procedencia y licencia.

Hasta entonces, la regla operativa es simple: **aprender patrones, escribir código propio y conservar la trazabilidad**.
