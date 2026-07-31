Windows/PowerShell commands:
- Firmware build (when PlatformIO is installed/on PATH): `pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3`
- Upload: `pio run -d Platformio/Dog-RGB -e seeed_xiao_esp32s3 -t upload`
- Serial monitor: `pio device monitor -d Platformio/Dog-RGB -b 115200`
- Existing host tests: from `Platformio/Dog-RGB`, `python -m unittest test.test_day_mode_static -v`
- Portal visual tests: from repo root, `npx playwright test tests/ap-portal-visual/ap-portal.visual.spec.ts --project=iphone-13-pro-max-chromium`
- Portal preview: `npm run ap-portal:extract`, then `npm run ap-portal:serve`
- Inventory/search: `rg --files`; `rg -n "pattern" path`
- Git checks: `git status --short`; `git diff --check`; `git diff -- path`
Current environment note: README's user-profile PlatformIO executable path is absent and no `pio` command was found during onboarding.