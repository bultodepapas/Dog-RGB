"""Pinned Montserrat subset for LVGL 8.4, no font tools required on the MCU."""
import hashlib
import importlib.metadata
import json
import shutil
from pathlib import Path
import subprocess
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT.parents[1] / 'Platformio/Dog-RGB/src/display/fonts'
RANGE = '0x20,0x27,0x2b,0x2d,0x30-0x39,0x41-0x5a,0x61-0x7a,0xc0-0xd6,0xd8-0xf6,0xf8-0xff'


def main():
    if importlib.metadata.version('fonttools') != '4.63.0':
        raise RuntimeError('Install the pinned requirements.txt before regenerating fonts')
    package = json.loads((ROOT / 'node_modules/lv_font_conv/package.json').read_text())
    if package['version'] != '1.5.3':
        raise RuntimeError('Run npm ci in tools/display-fonts before regenerating fonts')
    source = ROOT / 'source/Montserrat-variable.ttf'
    generated = ROOT / 'build'
    generated.mkdir(exist_ok=True)
    font = TTFont(source, recalcTimestamp=False)
    instance = instantiateVariableFont(font, {'wght': 600}, inplace=False)
    instance.recalcTimestamp = False
    instance.save(generated / 'Montserrat-600.ttf')
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'source/OFL.txt', OUT / 'OFL.txt')
    outputs = {}
    for size, name, glyphs in [(28, 'dog_name_28', RANGE), (18, 'dog_phone_18', '0x2b,0x30-0x39')]:
        for bpp in (2, 4):
            target = OUT / (name + '.c') if bpp == 4 else generated / (name + '.c')
            subprocess.run(['node', 'node_modules/lv_font_conv/lv_font_conv.js',
                            '--font', 'build/Montserrat-600.ttf', '--range', glyphs,
                            '--size', str(size), '--bpp', str(bpp), '--format', 'lvgl',
                            '--no-compress', '--lv-include', 'lvgl.h',
                            '-o', str(target.relative_to(ROOT) if target.is_relative_to(ROOT) else Path('../../Platformio/Dog-RGB/src/display/fonts') / target.name)],
                           cwd=ROOT, check=True)
            outputs[f'{name}-{bpp}bpp'] = {'c_bytes': target.stat().st_size,
                                         'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    result = {'font_source': 'https://github.com/google/fonts/tree/main/ofl/montserrat',
              'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'converter': 'lv_font_conv 1.5.3', 'instancer': 'fonttools 4.63.0',
              'weight': 600, 'format': 'LVGL 8.4 C, uncompressed, 4bpp selected',
              'name_range': RANGE, 'outputs': outputs}
    (ROOT / 'manifest.json').write_text(json.dumps(result, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
