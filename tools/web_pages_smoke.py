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
    "html_page": 33_000,
    "html_wifi_page": 36_000,
    # Fases 1–2 add persisted electrical calibration and a capabilities-driven
    # effect UI. Keep explicit ceilings, with headroom below each C++ reserve,
    # rather than silently dropping the guard as the pages grow.
    "html_config_page": 62_000,
    "html_dev_page": 35_000,
}

REQUIRED_SNIPPETS = [
    'id="lock_enabled"',
    "/api/lock",
    'id="track_load"',
    'id="track_csv"',
    'id="track_geo"',
    "max_points=250",
    "Ruta GPS",
    "Configurar Wi-Fi",
    "Estado Wi-Fi",
    "/api/wifi/ap",
    "/api/wifi/scan",
    'id="scan_btn"',
    'id="scan_results"',
    'id="brightness_slider"',
    'id="led_power_block"',
    'id="led_power_budget"',
    'data-mode-card="speed"',
    'data-theme="calm"',
    'id="color_swatches"',
    "Diagnostico AP",
    'id="diag-ap-start"',
    'id="led-power-estimated"',
    "<summary>JSON crudo</summary>",
    'id="sta_pass_hint"',
    'id="mdns_preview"',
    'id="ap_advisories"',
    "has_sta_pass",
]

# Raw protocol values must never reach the user as their confirmation. "ok" is
# what the API says; it is not what a person needs to read after saving.
FORBIDDEN_PATTERNS = [
    # The negative lookahead keeps `r.status === 'ok' ? ... : ...` out of it:
    # that is a comparison, not a value being rendered.
    (r"innerText\s*=\s*r\.status(?!\s*[=!])", "raw r.status rendered as user-facing text"),
    (r"textContent\s*=\s*r\.status(?!\s*[=!])", "raw r.status rendered as user-facing text"),
    (r"setApStatus\(\s*r\.status", "raw r.status rendered as user-facing text"),
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
    "saveLock",
    "buildColorSwatches",
    "updateModeCards",
    "syncBrightness",
    # Fase 1 of the UX review: each of these backs a fix that regressed silently
    # before, so the static check pins them in place.
    "applyConfig",
    "loadConfig",
    "revealFirstError",
    "confirmLeave",
    "handleTrackResize",
    "startScan",
    "renderNetworks",
    # Fase 2: the portal explaining itself.
    "updateStaPassHint",
    "updateMdnsPreview",
]

INLINE_CALLS = [
    "loadTrack",
    "refreshAll",
    "updateHome",
    "saveAp",
    "loadWifiStatus",
    "saveCfg",
    "resetCfg",
    "saveLock",
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


def check_interpolation_escaping(source: str) -> list[str]:
    """Every runtime value appended to a page must be HTML-escaped.

    A raw `page += some_value();` writes attacker-influenced text straight into
    markup. Only three shapes are safe: a compile-time raw literal, the shared
    stylesheet, or an explicit html_escape_attr() call.
    """
    failures: list[str] = []

    if not re.search(r"\bString\s+html_escape_attr\s*\(", source):
        failures.append("html_escape_attr() helper is missing from pages.cpp")

    for match in re.finditer(r"^\s*page \+= (.+?);\s*$", source, re.M):
        expr = match.group(1).strip()
        line = source.count("\n", 0, match.start()) + 1
        if expr.startswith('F(R"') or expr == "FPSTR(BASE_CSS)":
            continue
        if expr.startswith("html_escape_attr("):
            continue
        failures.append(
            f"pages.cpp:{line}: unescaped interpolation into markup: page += {expr};"
        )

    return failures


def check_csrf_header(source: str) -> list[str]:
    """Client POSTs must carry the header the server's csrf_ok() requires.

    A POST that forgets it is rejected with 403 at runtime, which is easy to
    miss until a user hits it.
    """
    failures: list[str] = []
    for index, line in enumerate(source.splitlines(), start=1):
        if "method:'POST'" not in line:
            continue
        if "X-Dog-Portal" not in line:
            failures.append(
                f"pages.cpp:{index}: POST without the X-Dog-Portal header: {line.strip()}"
            )

    # A native form post cannot set headers, so it would bypass the guard.
    if re.search(r'<form[^>]*\baction="/api/', source):
        failures.append("form posts directly to an /api/ route; use fetch() so the CSRF header is sent")

    return failures


def check_client_escaping(source: str) -> list[str]:
    """Markup built in the browser must escape interpolated values too.

    Today every value reaching innerHTML is a number or a literal, so nothing
    is exploitable -- but that is a property of the current data, not of the
    code. This keeps it a property of the code.
    """
    failures: list[str] = []
    for index, line in enumerate(source.splitlines(), start=1):
        if "innerHTML" not in line or "${" not in line:
            continue
        if "esc(" not in line:
            failures.append(
                f"pages.cpp:{index}: innerHTML interpolates without esc(): {line.strip()[:90]}"
            )
    if "innerHTML" in source and "function esc(" not in source:
        failures.append("esc() helper is missing from pages.cpp")
    return failures


def main() -> int:
    src = PAGES.read_text(encoding="utf-8")
    failures: list[str] = []

    failures.extend(check_interpolation_escaping(src))
    failures.extend(check_csrf_header(src))
    failures.extend(check_client_escaping(src))

    for snippet in REQUIRED_SNIPPETS:
        if snippet not in src:
            failures.append(f"missing required snippet: {snippet}")

    for pattern, why in FORBIDDEN_PATTERNS:
        for match in re.finditer(pattern, src):
            line = src.count("\n", 0, match.start()) + 1
            failures.append(f"pages.cpp:{line}: {why}: {match.group(0)}")

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
