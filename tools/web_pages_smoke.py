#!/usr/bin/env python3
"""Fast integrity and security checks for the generated AP portal."""

from __future__ import annotations

from hashlib import sha256
import gzip
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "webui" / "src" / "pages"
PREVIEW_DIR = ROOT / ".ap-portal-preview"
MANIFEST_PATH = ROOT / "webui" / "generated" / "manifest.json"
GENERATED_CPP = ROOT / "Platformio" / "Dog-RGB" / "src" / "web" / "generated_assets.cpp"
PORTAL_HTTP = ROOT / "Platformio" / "Dog-RGB" / "src" / "web" / "portal_http.cpp"
PORTAL_ASSETS = ROOT / "Platformio" / "Dog-RGB" / "src" / "web" / "portal_assets.cpp"
LEGACY_PAGES = ROOT / "Platformio" / "Dog-RGB" / "src" / "web" / "pages.cpp"
STYLE_MARKER = "<!-- DOG_RGB_INCLUDE:styles/app.css -->"

REQUIRED_BY_PAGE = {
    "index.html": (
        "/api/summary",
        "/api/status",
        "/api/track",
        "X-Dog-Portal",
    ),
    "wifi.html": (
        "/api/config",
        "baseCfg.wifi.sta_ssid",
        "/api/wifi/scan",
        "X-Dog-Portal",
    ),
    "config.html": (
        "/api/config",
        "/api/v1/led/capabilities",
        "/api/v1/led/scenes",
        "expected_generation",
        "palette_none_id",
        "scene_user_id_first",
        'id="scene_preview"',
        "let EFFECTS = []",
        "X-Dog-Portal",
    ),
    "dev.html": ("/api/dev", "diag-next-ap-retry", "diag-ap-failure-stage"),
}


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def canonical_input(data: bytes, repo_path: str) -> bytes:
    if data.startswith(b"\xef\xbb\xbf"):
        raise ValueError(f"UTF-8 BOM in {repo_path}")
    normalized = data.replace(b"\r\n", b"\n")
    if b"\r" in normalized:
        raise ValueError(f"bare CR line ending in {repo_path}")
    return normalized


def repo_file(repo_path: str) -> Path:
    path = (ROOT / repo_path).resolve()
    path.relative_to(ROOT)
    return path


def parse_cpp_array(source: str, symbol: str) -> bytes:
    match = re.search(
        rf"const uint8_t {re.escape(symbol)}_GZIP\[\] PROGMEM = \{{(.*?)\n\}};",
        source,
        re.S,
    )
    if match is None:
        raise ValueError(f"missing generated C++ array {symbol}_GZIP")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", match.group(1)))


def check_source(filename: str, source: str) -> list[str]:
    failures: list[str] = []
    label = f"webui/src/pages/{filename}"
    if source.count(STYLE_MARKER) != 1:
        failures.append(f"{label}: expected one shared CSS marker")
    if not re.search(r"<!doctype html>", source, re.I):
        failures.append(f"{label}: missing doctype")
    if not re.search(r'<html\s+lang="es">', source, re.I):
        failures.append(f'{label}: missing lang="es"')
    if re.search(r'<(?:script|img)[^>]+src=["\']https?://', source, re.I):
        failures.append(f"{label}: remote script/image dependency")
    if re.search(r'<link[^>]+href=["\']https?://', source, re.I):
        failures.append(f"{label}: remote stylesheet dependency")
    for snippet in REQUIRED_BY_PAGE[filename]:
        if snippet not in source:
            failures.append(f"{label}: missing contract {snippet!r}")
    if "method:'POST'" in source and "'X-Dog-Portal':'1'" not in source:
        failures.append(f"{label}: POST helper does not carry X-Dog-Portal")
    if filename == "config.html" and re.search(r"\bconst\s+EFFECTS\s*=\s*\[", source):
        failures.append(f"{label}: effect catalog must come from capabilities")
    return failures


def main() -> int:
    failures: list[str] = []
    if LEGACY_PAGES.exists():
        failures.append("legacy src/web/pages.cpp still exists")
    try:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"FAIL: cannot read manifest: {exc}", file=sys.stderr)
        return 1

    if manifest.get("schema_version") != 1:
        failures.append("manifest schema_version must be 1")
    cpp = GENERATED_CPP.read_text(encoding="utf-8")
    portal_http = PORTAL_HTTP.read_text(encoding="utf-8")
    portal_assets = PORTAL_ASSETS.read_text(encoding="utf-8")

    if "web_pages::" in portal_http or '"web/pages.h"' in portal_http:
        failures.append("portal_http.cpp still calls legacy String page builders")
    for contract in (
        "portal_assets::send",
        '"Accept-Encoding"',
        '"If-None-Match"',
        "web_assets::ROOT_PAGE",
        "web_assets::WIFI_PAGE",
        "web_assets::CONFIG_PAGE",
        "web_assets::DEV_PAGE",
    ):
        if contract not in portal_http:
            failures.append(f"portal_http.cpp: missing {contract}")
    for contract in (
        "send_P",
        '"Content-Encoding", "gzip"',
        '"Cache-Control", "no-cache"',
        '"Vary", "Accept-Encoding"',
        "etag_matches",
        "406",
        "304",
    ):
        if contract not in portal_assets:
            failures.append(f"portal_assets.cpp: missing {contract}")

    for item in manifest.get("inputs", []):
        try:
            path = repo_file(item["path"])
            actual_bytes = canonical_input(path.read_bytes(), item["path"])
            actual = digest(actual_bytes)
        except (KeyError, OSError, ValueError) as exc:
            failures.append(f"manifest input invalid: {exc}")
            continue
        if actual != item.get("sha256"):
            failures.append(f"stale manifest input: {item['path']}")
        if len(actual_bytes) != item.get("bytes"):
            failures.append(f"stale manifest input size: {item['path']}")
    for item in manifest.get("generated", []):
        try:
            path = repo_file(item["path"])
            actual = digest(path.read_bytes())
        except (KeyError, OSError, ValueError) as exc:
            failures.append(f"manifest generated output invalid: {exc}")
            continue
        if actual != item.get("sha256"):
            failures.append(f"stale generated output: {item['path']}")

    pages = manifest.get("pages", [])
    if len(pages) != 4:
        failures.append(f"manifest must contain four pages, found {len(pages)}")
    for page in pages:
        filename = Path(page.get("source", "")).name
        source_path = SOURCE_DIR / filename
        preview_path = PREVIEW_DIR / filename
        if filename not in REQUIRED_BY_PAGE or not source_path.is_file():
            failures.append(f"unknown or missing page source: {filename!r}")
            continue
        source = source_path.read_text(encoding="utf-8")
        failures.extend(check_source(filename, source))
        if not preview_path.is_file():
            failures.append(f"missing preview {preview_path.relative_to(ROOT)}")
            continue
        preview = preview_path.read_bytes()
        if STYLE_MARKER.encode() in preview:
            failures.append(f"{filename}: unresolved include marker in preview")
        if len(preview) != page.get("decoded_bytes"):
            failures.append(f"{filename}: decoded size does not match manifest")
        try:
            compressed = parse_cpp_array(cpp, page["symbol"])
            decoded = gzip.decompress(compressed)
        except (KeyError, OSError, EOFError, ValueError) as exc:
            failures.append(f"{filename}: invalid generated gzip: {exc}")
            continue
        if decoded != preview:
            failures.append(f"{filename}: firmware array differs from preview")
        if len(compressed) != page.get("gzip_bytes"):
            failures.append(f"{filename}: gzip size does not match manifest")
        if digest(compressed) != page.get("gzip_sha256"):
            failures.append(f"{filename}: gzip hash does not match manifest")
        if len(compressed) > page.get("gzip_budget_bytes", 0):
            failures.append(f"{filename}: gzip budget exceeded")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    total_raw = sum(page["decoded_bytes"] for page in pages)
    total_gzip = sum(page["gzip_bytes"] for page in pages)
    totals = manifest.get("totals", {})
    if totals.get("decoded_bytes") != total_raw:
        failures.append("manifest decoded total does not match pages")
    if totals.get("gzip_bytes") != total_gzip:
        failures.append("manifest gzip total does not match pages")
    if total_gzip > totals.get("gzip_budget_bytes", 0):
        failures.append("portal total gzip budget exceeded")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(
        f"web portal smoke OK: {len(pages)} pages, "
        f"{total_raw} B decoded, {total_gzip} B gzip"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
