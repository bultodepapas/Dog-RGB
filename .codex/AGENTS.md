# AGENTS.md — Smart LED Dog Collar (Codex Instructions)
> Root instructions (keep lean). For firmware- and hardware-specific rules, add overrides in:
> - `firmware/AGENTS.md`
> - `hardware/AGENTS.md`
> Use `.codex/skills/` for repeatable procedures (power budget audit, GNSS validation, etc.).

---

## 1) Project Context (minimum)
This repo builds a **smart, high-visibility LED dog collar**:
- LEDs: **APA102 / SK9822 (5V clocked)**
- MCU: **ESP32-S3** (Seeed Studio XIAO ESP32-S3)
- GNSS (Phase 1 MVP): **EBYTE E108-GN02 (10 Hz)** over UART (9600)
- Future: IMU, BLE app, advanced activity modes

Key folders:
- `firmware/` (Arduino framework; **PlatformIO** by default)
- `hardware/` (schematics/PCB/power notes)
- `docs/` (decisions, architecture, roadmap, tasks)
- `research/` (datasheets, references, calculations)

---

## 2) Non-negotiables (global)
### 2.1 Research-first, library-first (no reinvention)
- **Investigate first**: find mature, recognized libraries before coding.
- If a well-known library covers ≥80% of the need, **use it**.
- If you choose not to use a mature library, you must document why (compat, memory, license, stability, etc.).

### 2.2 `main.cpp` must be documented (always)
Any `main.cpp` change must keep:
- A header block: purpose, supported hardware, pin table, dependencies, build/flash steps, power/safety notes.
- Clear section separators and “why” comments (not just “what”).

### 2.3 Docs must stay in sync
If behavior, wiring, pins, thresholds, or dependencies change:
- Update `README.md` and relevant `docs/*` in the same change.

### 2.4 Hardware critique is required
If hardware is risky/suboptimal, you must say so and propose concrete alternatives:
- 5V boost sizing/decoupling, brownout risks, level shifting, EMI, waterproofing, strain relief, protection.

---

## 3) Workflow (global)
### 3.1 Small changes (default)
1) State goal + approach in 5–10 lines.
2) Implement in small, verifiable steps.
3) **Build** (see PlatformIO rule below).
4) Update docs if anything changed.

### 3.2 Large changes (multi-hour / high uncertainty)
Before coding, write a plan:
- `docs/PLANS/<YYYY-MM-DD>_<topic>.md`
Include: scope/non-scope, risks, library choices, steps + validation checkpoints.

---

## 4) PlatformIO (required)
- Firmware development **must use PlatformIO** unless the repo explicitly switches.
- Assume the firmware lives under `firmware/**` and is built/flashed via PlatformIO.
- If a PlatformIO environment name is needed, read `platformio.ini` and use the correct one.
- Always provide reproducible commands/steps (build/upload/monitor) in your change summary.

> If PlatformIO is not yet fully configured, add/repair `platformio.ini` and document it in `firmware/README.md`.

---

## 5) Validation & deliverables (global checklist)
For every feature/fix:
- [ ] Code compiles (PlatformIO)
- [ ] If `main.cpp` touched: header + comments remain correct
- [ ] README/docs updated if behavior/wiring/deps changed
- [ ] Repro steps for testing (serial logs + expected outcomes)
- [ ] Hardware risks + mitigations (if applicable)

---

## 6) Keep this file lean (avoid truncation)
- Keep root instructions concise and actionable.
- Put specifics in `firmware/AGENTS.md` and `hardware/AGENTS.md`.
- Put repeatable procedures into `.codex/skills/*/SKILL.md`.
