"""Render IdentityView A/B with the selected font variant; verify QR payloads."""
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
    parser.add_argument('--name', help='Local preview name')
    parser.add_argument('--phone', help='Local international phone')
    parser.add_argument('--bpp', choices=(2, 4), type=int, default=4)
    args = parser.parse_args()
    if bool(args.name) != bool(args.phone):
        parser.error('--name and --phone are required together')
    repo = ROOT.parents[1]
    firmware = repo / 'Platformio/Dog-RGB'
    fonts = firmware / 'src/display/fonts' if args.bpp == 4 else repo / 'tools/display-fonts/build'
    build = ROOT / 'build' if args.bpp == 4 else ROOT / 'build/identity-2bpp'
    output = ROOT / 'output' / f'identity-{args.bpp}bpp'
    subprocess.run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', f'-DIDENTITY_FONT_DIR={fonts}'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--target', 'display_identity_test', '-j', '4'], check=True)
    exe = build / 'display_identity_test'
    if exe.with_suffix('.exe').exists():
        exe = exe.with_suffix('.exe')
    command = [str(exe), str(output)]
    if args.name:
        command += [args.name, args.phone]
    subprocess.run(command, check=True)
    results = {}
    for line in (output / 'identity-expected.tsv').read_text(encoding='utf-8').splitlines():
        name, expected = line.split('\t')
        png = output / f'{name}.png'
        png.write_bytes(png_from_ppm(output / f'{name}.ppm'))
        with Image.open(png) as img:
            decoded = zxingcpp.read_barcodes(img)
        if [b.text for b in decoded] != ([] if expected == '-' else [expected]) or any(b.format != zxingcpp.BarcodeFormat.QRCode for b in decoded):
            raise RuntimeError(f'{name}: QR payload/count mismatch')
        results[name] = {'decoded_count': len(decoded), 'payload_matches': True,
                         'sha256': hashlib.sha256(png.read_bytes()).hexdigest()}
    files = [firmware / p for p in ('include/lv_conf.h', 'include/display/contact.h', 'include/display/contact_qr.h',
             'include/display/identity.h', 'include/display/identity_view.h', 'src/display/contact.cpp',
             'src/display/identity.cpp', 'src/display/ui/contact_qr.cpp', 'src/display/ui/identity_view.cpp')]
    files += [fonts / p for p in ('dog_name_28.c', 'dog_phone_18.c')]
    files += [ROOT / p for p in ('test_identity.cpp', 'render_identity.py', 'CMakeLists.txt')]
    manifest = {'lvgl': '8.4.0', 'bpp': args.bpp, 'decoder': importlib.metadata.version('zxing-cpp'),
                'images': results, 'source_sha256': {str(p.relative_to(repo)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
                'scope': 'Shared LVGL host render; physical panel, input and performance pending'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'{len(results)} IdentityView captures verified ({args.bpp} bpp)')


if __name__ == '__main__':
    main()
