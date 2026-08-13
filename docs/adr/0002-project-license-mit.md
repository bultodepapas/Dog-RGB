# ADR-0002: License original project material under MIT

**Status:** Accepted

**Date:** 2026-08-12

**Scope:** Project-authored source code, tooling, documentation, and future contributions to Dog-RGB unless a file explicitly states different terms.

## Context

Dog-RGB previously had no root license, so possession of the public source did not by itself grant permission to use, modify, or redistribute it. The project is intended to remain approachable for hobbyists, educators, maintainers, and commercial experimenters without imposing complex downstream obligations.

The repository also contains dependencies, vendor datasheets, references to external projects, and analysis inspired by WLED. Dog-RGB can license only material for which its copyright holders have the necessary rights.

## Decision

1. Dog-RGB adopts the **MIT License**, identified by SPDX as `MIT`.
2. Unless a file states otherwise, the root [`LICENSE`](../../LICENSE) covers original project source code, tooling, and documentation.
3. Users may use, copy, modify, merge, publish, distribute, sublicense, and sell covered material, provided that the copyright and license notice are preserved.
4. Third-party dependencies, vendor documents, trademarks, and externally authored material retain their own terms. Their presence in this repository does not relicense them under MIT.
5. The clean-room and provenance controls in [ADR-0001](0001-wled-clean-room-y-licencia-del-proyecto.md) remain active for WLED-inspired work and other external sources.
6. If substantial PCB, schematic, mechanical, or manufacturing source files are added later, maintainers may evaluate a dedicated open-hardware license. Any such exception must be explicit at the relevant directory or file boundary.

## Consequences

### Positive

- Anyone receives broad permission to reuse and adapt original Dog-RGB material.
- The short, permissive license keeps participation simple for a DIY project.
- The standard SPDX identifier is understood by common repository and dependency tooling.
- The required retained notice preserves attribution while allowing commercial and non-commercial use.

### Trade-offs

- Modified versions do not have to publish their source code.
- MIT provides the work without warranty and does not certify the collar's electrical, mechanical, RF, weather, thermal, or pet-safety properties.
- License compatibility and attribution still require review before importing third-party material.

## Implementation

- Keep the canonical MIT text in the root [`LICENSE`](../../LICENSE).
- Declare `MIT` in package or release metadata where a license field exists.
- Link the license from public entry points and distinguish original work from third-party material.
- Preserve copyright and license notices when redistributing substantial portions of the project.
