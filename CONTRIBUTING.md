# Contributing to Dog-RGB

Dog-RGB is a DIY electronics and firmware project. Small, understandable changes with evidence are preferred over product-scale infrastructure.

## Before changing code

1. Read the [project overview](README.md) and [architecture](docs/architecture.md).
2. Treat `Platformio/Dog-RGB` as the active firmware project.
3. Keep commercial-grade security, cloud infrastructure, and advanced developer tooling optional unless the issue explicitly requires them.
4. Preserve unrelated local changes; do not commit generated reports, local tokens, or `.env` files.
5. Follow [ADR-0001](docs/adr/0001-wled-clean-room-y-licencia-del-proyecto.md) for WLED-inspired work: design from concepts and observable behavior; do not copy source or assets while project licensing/provenance remains unresolved.

## Verification

Use the checks relevant to the changed area.

Firmware build and host contracts:

```powershell
cd Platformio\Dog-RGB
pio run -e seeed_xiao_esp32s3
python -m unittest discover -s test -p "test_*.py" -v
```

Portal behavior and static safety checks, from the repository root:

```powershell
npm ci
npm run smoke
npx playwright test --project=iphone-13-pro-max-chromium
```

Visual changes on Linux/macOS:

```bash
npm run ap-portal:visual
```

On PowerShell, use the explicit equivalent because the npm script sets its flag with POSIX syntax:

```powershell
$env:AP_PORTAL_VISUAL = '1'
npx playwright test tests/ap-portal-visual/ --project=iphone-13-pro-max-chromium
Remove-Item Env:AP_PORTAL_VISUAL
```

Only update committed screenshot baselines after reviewing the actual/expected/diff output. The baselines target the Linux Playwright 1.62.1 renderer used by CI; use the documented container workflow for deterministic regeneration.

Wokwi changes:

```powershell
cd Platformio\Dog-RGB
.\tools\wokwi.ps1 -Action prepare
.\tools\wokwi.ps1 -Action suite -TimeoutMs 90000
```

See [Testing and simulation](docs/testing.md) for prerequisites and narrower commands.

## Documentation rules

- English is the canonical language. Spanish translations may be kept for end-user guides.
- Put current behavior in a canonical guide or reference page.
- Label proposals as **Proposed** and dated investigations as **Historical snapshot**.
- Link to source files or symbols, but avoid treating volatile line numbers as permanent identifiers.
- State whether a feature is implemented, disabled by default, experimental, or planned.
- Keep commands runnable from the directory named immediately above the code block.
- Do not claim measured battery life, safety, waterproofing, or RF performance without physical evidence.
- Update [docs/README.md](docs/README.md) when adding, removing, or superseding a document.

## Pull-request checklist

- [ ] The active firmware still builds, when firmware changed.
- [ ] Relevant host/portal/Wokwi tests pass.
- [ ] Runtime defaults and API examples match the source.
- [ ] New write endpoints use the portal CSRF header and optional PIN guard.
- [ ] Documentation distinguishes current behavior from future ideas.
- [ ] No secret, token, generated evidence, build output, or local `.env` file is included.
- [ ] External inspiration has traceable provenance and follows the provisional clean-room policy.
