"""Build the shared LVGL view, verify it and export deterministic Activity/Connection PNGs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parent


def png_from_ppm(path: Path) -> bytes:
    magic, size, depth, rgb = path.read_bytes().split(b"\n", 3)
    assert (magic, size, depth) == (b"P6", b"240 280", b"255")
    assert len(rgb) == 240 * 280 * 3
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    scanlines = b"".join(b"\0" + rgb[y * 720:(y + 1) * 720] for y in range(280))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 240, 280, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "output")
    args = parser.parse_args()
    build = ROOT / "build"
    subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--target", "display_simulator", "display_port_test", "display_connection_adapter_test", "display_status_adapter_test", "display_contact_qr_test", "display_identity_test", "display_identity_store_test", "display_identity_console_test", "display_identity_api_0_test", "display_identity_api_1_test", "-j", "4"], check=True)
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"], check=True)
    exe = build / "display_simulator"
    if exe.with_suffix(".exe").exists():
        exe = exe.with_suffix(".exe")
    subprocess.run([str(exe), str(args.output.resolve())], check=True)
    hashes = {}
    for name in ("searching", "fix", "stale", "connection-ap", "connection-both", "connection-trying", "connection-idle", "connection-off", "connection-long", "status-no-data", "status-day", "status-alert", "status-paused", "status-fix", "owner-activity", "owner-wifi-demo", "owner-accent", "owner-enye-demo", "owner-long", "owner-wide-demo", "owner-invalid"):
        data = png_from_ppm(args.output / f"{name}.ppm")
        (args.output / f"{name}.png").write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    firmware = ROOT.parents[1] / "Platformio/Dog-RGB"
    inputs = ["include/display/ui_theme.h", "src/display/ui/ui_theme.cpp", "src/display/fonts/dog_name_14.c", "include/lv_conf.h", "src/display/ui/walk_view.cpp", "src/display/text_view.cpp", "src/display/connection.cpp", "src/display/ui/connection_view.cpp", "src/display/display.cpp", "src/display/lvgl_port.cpp",
              "src/display/connection_snapshot.cpp", "include/display/connection.h", "include/display/button.h", "include/display/inactivity.h",
              "include/display/status.h", "include/display/status_view.h", "src/display/status.cpp", "src/display/status_snapshot.cpp", "src/display/ui/status_view.cpp", "include/display/lvgl_port.h",
              "include/display/text_view.h", "include/display/walk_view.h", "include/display/connection_view.h"]
    result = {"lvgl": "8.4.0", "resolution": [240, 280], "png_sha256": hashes,
              "input_sha256": {p: hashlib.sha256((firmware / p).read_bytes()).hexdigest() for p in inputs},
              "scope": "shared LVGL rendering on PC; not physical panel or performance evidence"}
    (args.output / "manifest.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
