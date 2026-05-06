#!/usr/bin/env python3
"""Static smoke checks for embedded portal pages.

This catches lightweight regressions that the C++ build cannot see, such as
missing JavaScript functions referenced by inline handlers or route-critical
markup disappearing from the generated page templates.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
PAGES = ROOT / "Platformio" / "Dog-RGB" / "src" / "web" / "pages.cpp"

REQUIRED_SNIPPETS = [
    'id="mode_btn"',
    'id="track_load"',
    'id="track_csv"',
    'id="track_geo"',
    "max_points=250",
    "Ruta GPS",
    "Configurar Wi-Fi",
    "Estado Wi-Fi",
    "/api/wifi/ap",
    'id="brightness_slider"',
    'data-mode-card="speed"',
    'data-theme="calm"',
    'id="color_swatches"',
    "Diagnostico AP",
    'id="diag-ap-start"',
    "<summary>Raw JSON</summary>",
]

REQUIRED_FUNCTIONS = [
    "loadTrack",
    "refreshAll",
    "saveMode",
    "updateHome",
    "loadWifiStatus",
    "pollStaStatus",
    "validMdns",
    "saveAp",
    "saveCfg",
    "resetCfg",
    "buildColorSwatches",
    "updateModeCards",
    "syncBrightness",
]

INLINE_CALLS = [
    "loadTrack",
    "refreshAll",
    "saveMode",
    "updateHome",
    "saveAp",
    "loadWifiStatus",
    "saveCfg",
    "resetCfg",
    "setHome",
    "clearHome",
    "refresh",
]


def main() -> int:
    src = PAGES.read_text(encoding="utf-8")
    failures: list[str] = []

    for snippet in REQUIRED_SNIPPETS:
        if snippet not in src:
            failures.append(f"missing required snippet: {snippet}")

    for fn in REQUIRED_FUNCTIONS:
        pattern = rf"\bfunction\s+{re.escape(fn)}\s*\("
        if not re.search(pattern, src):
            failures.append(f"missing function definition: {fn}()")

    for fn in INLINE_CALLS:
        call = f'onclick="{fn}('
        if call in src:
            pattern = rf"\bfunction\s+{re.escape(fn)}\s*\("
            if not re.search(pattern, src):
                failures.append(f"inline onclick references undefined function: {fn}()")

    if "setInterval(loadTrack" in src:
        failures.append("route preview must not be auto-polled")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("web_pages_smoke: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
