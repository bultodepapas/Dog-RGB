# Show Mode Manual Test Checklist

> **Document status:** Current manual validation artifact refreshed for scene-based Show on 2026-08-13. Automated contracts cover structure; the physical visual result still requires human judgment.

## Setup

- Flash the active `seeed_xiao_esp32s3` environment.
- Connect the configured one- or two-strip layout with a safe, measured brightness.
- Make `/config` and `/dev` reachable through AP or STA.
- Record build metadata, LED count, initial mode, GNSS state, Wi-Fi state, and AP clients.

## 1. Enter Show mode

1. Select **Show** on `/config` and save.
2. Open `/dev` and confirm the reported LED mode is `show`.
3. Confirm that `/api/v1/led/state` reports `scene.playback:"show"`, a valid scene ID/name, and matching effect/palette metadata.

Expected: the body starts a scene while status pixels continue to show Wi-Fi/GNSS. No normal mode, including Simple or manual scene playback, claims the reserved status pixels.

## 2. Observe one complete shuffled bag

First query `/api/v1/led/scenes` and count the four built-ins plus occupied user scenes with `show_eligible:true`. Allow roughly 30 seconds of visible time per eligible scene; Welcome and Day Mode pause this clock.

| Sequence | Approximate time | Scene | ID | Effect/palette/transition notes |
| ---: | --- | --- | ---: | --- |
| 1 | 00:00 |  |  |  |
| 2 | 00:30 |  |  |  |
| 3 | 01:00 |  |  |  |
| 4 | 01:30 |  |  |  |
| 5 | 02:00 |  |  |  |
| 6 | 02:30 |  |  |  |
| 7 | 03:00 |  |  |  |
| 8 | 03:30 |  |  |  |

Rows 5–8 apply only when those user slots are occupied and eligible.

Expected:

- every eligible scene ID occurs exactly once before the bag repeats;
- the first item of the next bag differs from the previous bag's last item;
- each scene uses its declared transition and does not produce an obvious blank/frozen strip;
- the four built-ins match the documented effect, palette, color and relative body level;
- a user scene saved while active remains visually snapshotted/stale until explicitly reapplied.

## 3. Status and Day Mode

- Change AP/client and GNSS states and confirm the two status pixels remain readable.
- Enable Day Mode and test with trusted GNSS time inside/outside the 06:00–16:00 UTC-5 window.

Expected: Day Mode turns off effect pixels during its window but preserves status indicators. Missing/stale time leaves effects on.

## 4. Two-strip behavior

When two strips are configured, confirm each scene follows its declared mirror/A-B branches and that physical A-forward/B-reverse orientation looks intentional. Record a mounting mismatch separately from a recipe mismatch.

## Optional explicit Wi-Fi-OFF test

The firmware retains homogeneous eligibility after an explicit Wi-Fi-OFF state plus stable GNSS, but semantic status ownership remains reserved. Automatic idle AP shutdown currently stops SoftAP without forcing that state. If tested through an experimental control path, confirm scenes still do not cover status pixels.

## Pass record

- [ ] Show is selectable and persists.
- [ ] `/dev` and LED state report mode, scene, effect and palette correctly.
- [ ] Every eligible scene appears once per bag.
- [ ] Bag-boundary non-repeat behavior passes.
- [ ] Transitions and supported color evolution look acceptable.
- [ ] Status pixels survive normal Show operation.
- [ ] Day Mode preserves status and controls only the effect segment.
- [ ] Both strips follow mirror/branch declarations and physical orientation.
- [ ] Current, voltage, and temperature remain within the build's measured limits.

Link any failure to logs, firmware revision, configuration export, and photos/video. See [LED UI](led_ui_spec.md) and [LED effects](led_effects.md) for the canonical behavior.
