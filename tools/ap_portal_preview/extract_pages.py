#!/usr/bin/env python3
"""Extract embedded AP portal HTML pages from pages.cpp for local preview."""

from pathlib import Path
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


def extract_page(source: str, name: str, base_css: str) -> str:
    body = function_body(source, name)
    parts: list[str] = []
    pattern = re.compile(r'page \+= F\(R"([A-Z]+)\((.*?)\)\1"\);|page \+= FPSTR\(BASE_CSS\);', re.S)
    for match in pattern.finditer(body):
        if match.group(0) == "page += FPSTR(BASE_CSS);":
            parts.append(base_css)
        else:
            parts.append(match.group(2))
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
