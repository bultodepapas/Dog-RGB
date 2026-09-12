# I6c — opt-in inactivity bench

Experimental software preparation after the owner confirmed I6a's improved
appearance, physical BOOT navigation and wake-only first press. Real GPS/LED/HTTP
acceptance V3 remains open. See the [incremental plan](../../../docs/PLANS/2026-09-12_display-incremental-delivery.md)
and [I6c evidence](../../../docs/baselines/display-i6c-2026-09-12.md).

## Behavior

The timer starts **disabled on every boot**, including the product target.
Only the stage-3 diagnostic can enable it for an experiment. Nothing is saved
to configuration/NVS and no portal setting or new compile target is added.

- `i`: enable/restart a 30,000 ms interaction timer if LVGL initialized.
- `o`: disable it; an already dark screen stays dark until explicitly woken.
- BOOT short release / `n`: renew the timer; if dark, redraw/wake the selected
  page without advancing. The next release advances normally.
- Existing `b` when turning light on and `d` when resuming the service renew
  the timer. Direct bench page/backend/fixture commands do not renew it.

Only backlight turns off. Sampling, LVGL rendering, GPS, LED policy, radio and
storage continue under their existing rules. GPS samples, Wi-Fi traffic, held
buttons and rejected bounce do not renew the timer. This is screen inactivity,
not a statement about dog rest, MCU sleep or measured battery savings.

Expiration is checked before input, so a valid release at the deadline wakes
the same page. The expired state latches across clock rollover until interaction
or reconfiguration. Redraws, including two-phase returns from bars/basic text,
cannot relight an expired screen. There is no animation to coordinate yet.

`[LCD]` adds `idle_ms` (0 or 30000), `idle` (latched expiration), and `timeouts`
(cumulative expirations). Drawing-stat reset `r` does not reset interaction or
timeout counts. `idle=0` does not imply light on: manual darkness/pause are
independent. The capture helper accepts `i/o` and summarizes these fields.

## Checks and remaining gate

The existing four renderer contracts now include the deadline boundary,
rollover/latching, stale data while dark, redraw immunity, held/bouncing input,
twenty timeout/wake cycles over both pages, disabling while dark, paused-service
recovery and default-off behavior. The same nine PNGs remain pixel-identical to
I6a; source manifests are retained separately for each run.

Run `python tools/display-simulator/render.py` from the repository root;
firmware tests/builds follow [testing](../../../docs/testing.md). Physical logs
prove reported state, not visual response; retain separate user observations.
Product activation/configuration and any portable power claim require the
remaining V3 and electrical evidence. I6b animation remains a separate proposal.
