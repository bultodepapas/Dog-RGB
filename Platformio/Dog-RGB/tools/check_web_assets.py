"""PlatformIO pre-build guard for committed web assets.

This intentionally uses only Python's standard library. It verifies hashes and
never invokes Node, npm, or the network, so firmware builds remain offline.
"""

from hashlib import sha256
import json
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # provided by PlatformIO/SCons


PROJECT_DIR = Path(env.subst("$PROJECT_DIR")).resolve()  # type: ignore[name-defined]
ROOT = PROJECT_DIR.parents[1]
MANIFEST = ROOT / "webui" / "generated" / "manifest.json"


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def safe_repo_file(repo_path: str) -> Path:
    candidate = (ROOT / Path(repo_path)).resolve()
    try:
        candidate.relative_to(ROOT)
    except ValueError as exc:
        raise RuntimeError(f"web asset manifest escapes repository: {repo_path}") from exc
    return candidate


if not MANIFEST.is_file():
    raise RuntimeError("Missing webui/generated/manifest.json; run npm run webui:build")

try:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
except (OSError, UnicodeError, json.JSONDecodeError) as exc:
    raise RuntimeError(f"Cannot read web asset manifest: {exc}") from exc

if manifest.get("schema_version") != 1:
    raise RuntimeError("Unsupported web asset manifest; run npm run webui:build")

stale: list[str] = []
fingerprint_parts: list[str] = []
for item in manifest.get("inputs", []):
    repo_path = item.get("path", "")
    path = safe_repo_file(repo_path)
    if not path.is_file():
        stale.append(f"missing {repo_path}")
        continue
    actual = digest(path.read_bytes())
    if actual != item.get("sha256"):
        stale.append(repo_path)
    fingerprint_parts.append(f"{repo_path}\0{actual}\n")

source_hash = digest("".join(fingerprint_parts).encode("utf-8"))
if source_hash != manifest.get("source_sha256"):
    stale.append("source fingerprint")

for item in manifest.get("generated", []):
    repo_path = item.get("path", "")
    path = safe_repo_file(repo_path)
    if not path.is_file() or digest(path.read_bytes()) != item.get("sha256"):
        stale.append(repo_path or "generated output")

if stale:
    details = ", ".join(dict.fromkeys(stale))
    raise RuntimeError(
        f"Generated web assets are stale ({details}); run npm run webui:build"
    )

print(
    "Web UI assets verified: "
    f"{len(manifest.get('pages', []))} pages, source {source_hash[:12]}"
)
