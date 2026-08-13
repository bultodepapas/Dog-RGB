# Show Mode Manual Test Checklist

> **Document status:** Historical/manual validation artifact refreshed for the current interface on 2026-08-12. Automated contracts cover structure; the physical visual result still requires human judgment.

## Setup

- Flash the active `seeed_xiao_esp32s3` environment.
- Connect the configured one- or two-strip layout with a safe, measured brightness.
- Make `/config` and `/dev` reachable through AP or STA.
- Record build metadata, LED count, initial mode, GNSS state, Wi-Fi state, and AP clients.

## 1. Enter Show mode

1. Select **Show** on `/config` and save.
2. Open `/dev` and confirm the reported LED mode is `show`.
3. Confirm that the current Show effect exposes a valid name and ID.

Expected: the effect segment starts a demo while status pixels continue to show Wi-Fi/GNSS. Simple mode is the only normal mode that intentionally claims the entire strip.

## 2. Observe one complete shuffled bag

Allow at least six minutes because each of the 12 effects normally runs for about 30 seconds.

| Sequence | Approximate time | Effect | ID | Transition/color notes |
| ---: | --- | --- | ---: | --- |
| 1 | 00:00 |  |  |  |
| 2 | 00:30 |  |  |  |
| 3 | 01:00 |  |  |  |
| 4 | 01:30 |  |  |  |
| 5 | 02:00 |  |  |  |
| 6 | 02:30 |  |  |  |
| 7 | 03:00 |  |  |  |
| 8 | 03:30 |  |  |  |
| 9 | 04:00 |  |  |  |
| 10 | 04:30 |  |  |  |
| 11 | 05:00 |  |  |  |
| 12 | 05:30 |  |  |  |

Expected:

- all IDs `0..11` occur once before the bag repeats;
- the first item of the next bag differs from the previous bag's last item;
- transitions are brief and do not produce an obvious blank/frozen strip;
- base color evolves where the effect supports it;
- `RAINBOW`, `GRADIENT_WAVE`, and `FIRE` need not reflect the random base color directly.

## 3. Status and Day Mode

- Change AP/client and GNSS states and confirm the two status pixels remain readable.
- Enable Day Mode and test with trusted GNSS time inside/outside the 06:00–16:00 UTC-5 window.

Expected: Day Mode turns off effect pixels during its window but preserves status indicators. Missing/stale time leaves effects on.

## 4. Two-strip behavior

When two strips are configured, confirm that both use the same Show effect and parameters at each transition. Their physical orientation may make the pattern look mirrored or repetitive; record that as a design observation unless the data streams actually diverge.

## Optional explicit Wi-Fi-OFF test

The firmware retains homogeneous rendering after an explicit Wi-Fi-OFF state plus stable GNSS. Automatic idle AP shutdown currently stops SoftAP without forcing that state, so this case is not part of the normal field policy. If tested through an experimental control path, confirm the Show effect covers the status pixels only after the documented stability delay.

## Pass record

- [ ] Show is selectable and persists.
- [ ] `/dev` reports the current mode/effect correctly.
- [ ] All 12 effects appear once per bag.
- [ ] Bag-boundary non-repeat behavior passes.
- [ ] Transitions and supported color evolution look acceptable.
- [ ] Status pixels survive normal Show operation.
- [ ] Day Mode preserves status and controls only the effect segment.
- [ ] Both strips stay synchronized.
- [ ] Current, voltage, and temperature remain within the build's measured limits.

Link any failure to logs, firmware revision, configuration export, and photos/video. See [LED UI](led_ui_spec.md) and [LED effects](led_effects.md) for the canonical behavior.
