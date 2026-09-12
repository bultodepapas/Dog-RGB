"""Verify and export frames from the real service/port, with fake SPI and NVS."""
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image
import zxingcpp
from render import png_from_ppm

ROOT = Path(__file__).resolve().parent


def main():
    build = ROOT / "build"
    subprocess.run(["ctest", "--test-dir", str(build), "-R", "^display_port_", "--output-on-failure"], check=True)
    output = ROOT / "output" / "port"
    output.mkdir(parents=True, exist_ok=True)
    whatsapp = "https://wa.me/100000000000"
    expected = {"boot-identity": whatsapp, "identity": whatsapp, "renamed": whatsapp,
                "call": "tel:+100000000001", "restored": whatsapp,
                **{name: None for name in ("activity-four", "connection-four", "status-four",
                                           "long-name", "disabled-qr", "cleared")}}
    images = {}
    for name, payload in expected.items():
        data = png_from_ppm(build / "port-test-output" / f"{name}.ppm")
        path = output / f"{name}.png"
        path.write_bytes(data)
        decoded = zxingcpp.read_barcodes(Image.open(path))
        assert [code.text for code in decoded] == ([payload] if payload else []), name
        images[name] = {"sha256": hashlib.sha256(data).hexdigest(), "decoded_count": len(decoded), "payload_matches": True}
    manifest = {"scope": "Real display service, LVGL port and store; fake Arduino/SPI/NVS. No physical scan or timing claim.",
                "images": images}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(images)} port frames and exact QR payloads")


if __name__ == "__main__":
    main()
