"""Render the firmware ContactQr with LVGL and independently decode every fixture."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess

from PIL import Image
import zxingcpp
from render import ROOT, png_from_ppm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'output' / 'qr')
    parser.add_argument('--name', help='Optional local preview name; output is not a public fixture')
    parser.add_argument('--phone', help='Explicit international phone for the local preview')
    args = parser.parse_args()
    if bool(args.name) != bool(args.phone):
        parser.error('--name and --phone must be supplied together')
    build = ROOT / 'build'
    subprocess.run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'display_contact_qr_test', '-j', '4'], check=True)
    exe = build / 'display_contact_qr_test'
    if exe.with_suffix('.exe').exists():
        exe = exe.with_suffix('.exe')
    command = [str(exe), str(args.output.resolve())]
    if args.name:
        command += [args.name, args.phone]
    subprocess.run(command, check=True)
    results = {}
    for line in (args.output / 'qr-expected.tsv').read_text(encoding='utf-8').splitlines():
        name, expected = line.split('\t')
        ppm = args.output / f'{name}.ppm'
        png = args.output / f'{name}.png'
        png.write_bytes(png_from_ppm(ppm))
        with Image.open(png) as img:
            decoded = zxingcpp.read_barcodes(img)
        actual = [barcode.text for barcode in decoded]
        target = [] if expected == '-' else [expected]
        if actual != target or any(b.format != zxingcpp.BarcodeFormat.QRCode for b in decoded):
            raise RuntimeError(f'{name}: QR payload/count mismatch')
        results[name] = {'decoded_count': len(decoded), 'payload_matches': True,
                         'sha256': hashlib.sha256(png.read_bytes()).hexdigest()}
    firmware = ROOT.parents[1] / 'Platformio/Dog-RGB'
    paths = ['include/lv_conf.h', 'include/display/contact.h', 'include/display/contact_qr.h',
             'src/display/contact.cpp', 'src/display/ui/contact_qr.cpp']
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    lvgl_root = next(line.split('=', 1)[1] for line in cache.splitlines()
                     if line.startswith('LVGL_SOURCE_DIR:PATH='))
    encoder = Path(lvgl_root) / 'src/extra/libs/qrcode'
    manifest = {'lvgl': '8.4.0', 'decoder': importlib.metadata.version('zxing-cpp'),
                'images': results, 'source_sha256': {p: hashlib.sha256((firmware / p).read_bytes()).hexdigest() for p in paths},
                'encoder_sha256': {p: hashlib.sha256((encoder / p).read_bytes()).hexdigest() for p in ('qrcodegen.c', 'qrcodegen.h')},
                'runner_sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in ('test_contact_qr.cpp', 'render_qr.py', 'render.py', 'CMakeLists.txt', 'requirements-qr.txt')},
                'scope': 'LVGL RGB565 host pixels; physical scanning/performance pending'}
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'{len(results)} QR/fallback captures verified; output: {args.output}')


if __name__ == '__main__':
    main()
