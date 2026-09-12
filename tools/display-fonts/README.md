# Identity fonts — VIS-1b

Montserrat, weight 600, from the [Google Fonts source](https://github.com/google/fonts/tree/main/ofl/montserrat).
The downloaded variable TTF and its [SIL OFL 1.1](source/OFL.txt) are checked in;
`manifest.json` pins its SHA-256. Generated C assets also carry the license in
`Platformio/Dog-RGB/src/display/fonts/OFL.txt`.

Rebuild from the repository root (optional host authoring dependencies):

```powershell
python -m pip install -r tools/display-fonts/requirements.txt
npm ci --ignore-scripts --prefix tools/display-fonts
python tools/display-fonts/generate.py
```

`fonttools==4.63.0` instantiates weight 600 without recalculating timestamps;
`lv_font_conv==1.5.3` emits uncompressed LVGL C with kerning. Production uses
4 bpp: name 28 px, phone 18 px. Name subset covers the explicit identity alphabet
(ASCII letters/digits, space/hyphen/apostrophe, Latin-1 letters) plus `+`;
phone subset contains only `+0123456789`. No runtime TTF/Unicode library.
The firmware C assets are committed, so normal builds need neither npm nor Python.

The same recipe generates 2 bpp alternatives under ignored `build/`. Compare
through `render_identity.py --bpp 2` and default 4 bpp; both compile/render with
LVGL 8.4 and independent QR checks. Measured Xtensa `-Os` object text:

| Variant | Name | Phone | Total |
| --- | ---: | ---: | ---: |
| 2 bpp | 12,216 B | 550 B | 12,766 B |
| 4 bpp, selected | 23,148 B | 911 B | 24,059 B |

Each font additionally has 8 B BSS cache. These are isolated object measurements,
not the final linked identity page cost. `c_bytes` in the manifest is source
file length, not flash usage. 4 bpp preserves smoother small curves/diagonals;
2 bpp remains a measured option if the integrated flash budget needs trimming.
