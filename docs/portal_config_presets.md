# Portal Configuration Presets

> **Document status:** Proposed optional feature. No preset storage, API, or selector is implemented in the active firmware as of 2026-08-12.

Presets would let a user apply a named group of existing configuration fields without editing ten ranges individually. They should remain an optional convenience layer over the versioned configuration schema, not a second configuration system.

## Proposed scope

- Ship a small set of built-in profiles such as **Calm**, **Active**, and **Sport**.
- Allow preview, apply, and subsequent manual editing.
- Reuse existing `POST /api/config` validation and transactional persistence.
- Keep Home, station credentials, AP credentials, lock PIN, metrics, sessions, and routes outside a visual preset.
- Avoid custom user-preset persistence in the first iteration; hard-coded defaults are easier to recover and migrate.

## Candidate model

```json
{
  "id": "calm",
  "label": "Calm",
  "mode": "speed",
  "brightness": 60,
  "ranges": [2, 4, 6, 8, 10, 12, 14, 16, 18],
  "effects": [
    {"a": 2, "b": 2, "speed": 30, "intensity": 60}
  ]
}
```

The example abbreviates the `effects` array. An applied Speed preset must provide all ten entries because the current API validates that collection atomically. Effect IDs and input ranges must come from [LED effects](led_effects.md) and [Portal configuration](portal_config.md).

## Candidate built-ins

| Preset | Intent | Likely choices |
| --- | --- | --- |
| Calm | Low brightness and gentle motion | `SOLID`, `PULSE`, `BREATH` |
| Active | Medium brightness and clear movement | `CHASE`, `COMET`, `SINELON` |
| Sport | Faster, high-energy output | `JUGGLE`, `BPM`, `RAINBOW` |

Geofence can be a mode shortcut, but `fence_max_m` should be confirmed by the user instead of hidden in a generic visual profile.

## Acceptance criteria for a future implementation

- Applying a preset uses the same validation, rollback, and error envelope as other configuration writes.
- The UI shows exactly which fields will change before saving.
- Applying a preset is reversible through the normal defaults/reset workflow.
- Unknown preset IDs fail without mutating configuration.
- Existing schema-version migration and A/B storage recovery continue to work.
- Firmware size and portal-source smoke limits remain green.
