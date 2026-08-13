# ADR-0001: Aprendizaje clean-room de WLED y control de procedencia

**Estado:** Aceptado; la selección de licencia fue resuelta por [ADR-0002](0002-project-license-mit.md)

**Fecha:** 2026-08-12

**Alcance:** Firmware, portal, assets, documentación y futuras contribuciones de RGB Dog.

> **Actualización — 2026-08-12:** Dog-RGB adoptó la [licencia MIT](../../LICENSE). Las referencias históricas de esta ADR a una licencia todavía pendiente describen el contexto en el que se tomó la decisión. La política clean-room y los controles de procedencia continúan vigentes porque MIT no concede derechos sobre material de terceros.

## Contexto

RGB Dog está estudiando patrones de [WLED v16.0.1](https://github.com/wled/WLED/tree/v16.0.1), cuyo código se publica bajo [EUPL-1.2](https://github.com/wled/WLED/blob/v16.0.1/LICENSE). Cuando se adoptó originalmente esta decisión todavía no había un archivo `LICENSE` o `COPYING` en la raíz de RGB Dog. [ADR-0002](0002-project-license-mit.md) resolvió posteriormente esa carencia mediante la licencia MIT.

El análisis técnico recomienda adaptar conceptos de WLED —límite de corriente, registro y metadatos de efectos, segmentos semánticos, paletas, transiciones, presets y empaquetado web—, pero RGB Dog tiene un dominio, arquitectura y restricciones diferentes.

La selección de MIT para el material original de Dog-RGB no determina por sí sola que código o assets externos sean compatibles, ni elimina sus requisitos de atribución, procedencia y distribución.

## Decisión

Para cualquier trabajo inspirado por WLED o por otra fuente externa:

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
6. Los binarios y paquetes pueden identificar el material original de Dog-RGB como MIT, pero deberán conservar los avisos y licencias exigidos por todos sus componentes de terceros.

Esta ADR no ofrece una conclusión legal sobre compatibilidad entre MIT, EUPL-1.2 u otras licencias. [ADR-0002](0002-project-license-mit.md) documenta únicamente la licencia elegida para el material original de Dog-RGB.

## Consecuencias

### Positivas

- Permite avanzar con las mejoras de arquitectura sin contaminar la procedencia del código.
- Separa claramente la licencia del proyecto de los permisos necesarios para importar material externo.
- Evita importar la complejidad estructural y compatibilidad histórica de WLED.
- Deja una regla verificable para contribuciones futuras.

### Costos

- Algunas funciones requerirán más diseño y pruebas que un port directo.
- No se podrán importar rápidamente efectos o assets de WLED.
- Cualquier reutilización literal externa sigue requiriendo análisis individual.

## Criterio para revisar esta ADR

Esta política puede revisarse cuando:

1. exista un inventario completo de dependencias y material de terceros;
2. se amplíe la política de atribución y contribuciones;
3. se proponga reutilización literal con procedencia y licencia verificables;
4. cambie la estrategia de licencia documentada en ADR-0002.

La regla operativa sigue siendo simple: **aprender patrones, escribir código propio y conservar la trazabilidad**.
