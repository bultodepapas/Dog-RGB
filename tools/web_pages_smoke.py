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

PAGE_BUDGETS = {
    "html_page": 28_000,
    "html_wifi_page": 26_000,
    "html_config_page": 40_000,
    "html_dev_page": 28_000,
}

REQUIRED_SNIPPETS = [
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
    "<summary>JSON crudo</summary>",
]

REQUIRED_FUNCTIONS = [
    "loadTrack",
    "refreshAll",
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
    "updateHome",
    "saveAp",
    "loadWifiStatus",
    "saveCfg",
    "resetCfg",
    "setHome",
    "clearHome",
    "refresh",
]


def raw_literal_size(source: str, token: str) -> int:
    match = re.search(rf'R"{token}\((.*?)\){token}"', source, re.S)
    return len(match.group(1).encode("utf-8")) if match else 0


def estimate_page_sizes(source: str) -> dict[str, tuple[int, int]]:
    base_css = raw_literal_size(source, "CSS")
    estimates: dict[str, tuple[int, int]] = {}
    for name in PAGE_BUDGETS:
        match = re.search(rf"String web_pages::{name}\(\) \{{(.*?)\n\}}", source, re.S)
        if not match:
            continue
        body = match.group(1)
        total = 0
        for part in re.findall(r'page \+= F\(R"[A-Z]+\((.*?)\)[A-Z]+"\);', body, re.S):
            total += len(part.encode("utf-8"))
        if "FPSTR(BASE_CSS)" in body:
            total += base_css
        reserve_match = re.search(r"page\.reserve\((\d+)\)", body)
        reserve = int(reserve_match.group(1)) if reserve_match else 0
        estimates[name] = (total, reserve)
    return estimates


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

    page_sizes = estimate_page_sizes(src)
    for name, budget in PAGE_BUDGETS.items():
        if name not in page_sizes:
            failures.append(f"missing page size estimate: {name}")
            continue
        size, reserve = page_sizes[name]
        if size > budget:
            failures.append(f"{name} is {size} bytes, over budget {budget}")
        if reserve < size:
            failures.append(f"{name} reserve {reserve} is below estimated size {size}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("web_pages_smoke: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
