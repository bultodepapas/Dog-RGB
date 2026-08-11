#!/usr/bin/env python3
"""Extract embedded AP portal HTML pages from pages.cpp for local preview."""

from pathlib import Path
import json
import os
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
PAGES = ROOT / "Platformio" / "Dog-RGB" / "src" / "web" / "pages.cpp"
OUT_DIR = ROOT / ".ap-portal-preview"

PAGE_BUILDERS = {
    "html_page": "index.html",
    "html_wifi_page": "wifi.html",
    "html_config_page": "config.html",
    "html_dev_page": "dev.html",
}


def raw_literal(source: str, token: str) -> str:
    match = re.search(rf'R"{token}\((.*?)\){token}"', source, re.S)
    if not match:
        raise ValueError(f"missing raw literal token {token}")
    return match.group(1)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"String web_pages::{name}\(\) \{{(.*?)\n\}}", source, re.S)
    if not match:
        raise ValueError(f"missing page builder {name}")
    return match.group(1)


# Every `page += ...;` statement must match one of these shapes. The catch-all
# branch is deliberately last so it only fires on something unrecognised, which
# is then reported instead of being silently dropped from the preview.
STATEMENT = re.compile(
    r'page \+= F\(R"([A-Z]+)\((.*?)\)\1"\);'
    r"|page \+= FPSTR\(BASE_CSS\);"
    r"|page \+= html_escape_attr\(([^;\n]+?)\);"
    r"|page \+= ([^;\n]+);",
    re.S,
)

# Runtime values the firmware interpolates, keyed by the C++ expression. Tests
# override these via AP_PORTAL_SUBST to exercise hostile input through the same
# escaping the firmware applies.
DEFAULT_SUBSTITUTIONS = {"wifi_mgr::ssid()": ""}


def escape_attr(value: str) -> str:
    """Mirror of html_escape_attr() in pages.cpp."""
    return (
        value.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&#39;")
    )


def substitutions() -> dict[str, str]:
    values = dict(DEFAULT_SUBSTITUTIONS)
    raw = os.environ.get("AP_PORTAL_SUBST")
    if raw:
        values.update(json.loads(raw))
    return values


def extract_page(source: str, name: str, base_css: str) -> str:
    body = function_body(source, name)
    subst = substitutions()
    parts: list[str] = []
    consumed = 0

    for match in STATEMENT.finditer(body):
        text = match.group(0)
        if text == "page += FPSTR(BASE_CSS);":
            parts.append(base_css)
        elif match.group(2) is not None:
            parts.append(match.group(2))
        elif match.group(3) is not None:
            expr = match.group(3).strip()
            if expr not in subst:
                raise ValueError(
                    f"{name}: no preview value for interpolated expression {expr!r}; "
                    f"add it to DEFAULT_SUBSTITUTIONS"
                )
            parts.append(escape_attr(subst[expr]))
        else:
            line = source.count("\n", 0, source.index(body) + match.start()) + 1
            raise ValueError(
                f"{name}: unrecognised page statement at pages.cpp:{line}: {text.strip()} "
                f"(preview would silently omit it)"
            )
        consumed += 1

    if consumed != body.count("page += "):
        raise ValueError(
            f"{name}: parsed {consumed} statements but found {body.count('page += ')} "
            f"'page +=' occurrences; the preview would not match the firmware"
        )
    if not parts:
        raise ValueError(f"no HTML fragments extracted for {name}")
    return "".join(parts)


def main() -> int:
    source = PAGES.read_text(encoding="utf-8")
    base_css = raw_literal(source, "CSS")
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    for builder, filename in PAGE_BUILDERS.items():
        html = extract_page(source, builder, base_css)
        (OUT_DIR / filename).write_text(html, encoding="utf-8")
        print(f"wrote {OUT_DIR / filename}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"extract_pages: {exc}", file=sys.stderr)
        raise SystemExit(1)
