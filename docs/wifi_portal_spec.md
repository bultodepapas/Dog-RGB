# Wi-Fi, Access Point, and Captive Portal

**Status:** Current firmware behavior, verified 2026-08-12.

Dog-RGB supports SoftAP, station mode, and AP+STA. The AP is both the first-run setup surface and the recovery path when the home network or GNSS state is unavailable.

## Defaults

| Setting | Default |
| --- | --- |
| AP SSID | `DogRGB` |
| AP password | `Dog12345` (change before normal use) |
| AP address | `192.168.4.1` |
| AP channel | 1 when station mode does not constrain it |
| AP clients | Maximum 2 |
| mDNS name | `dog-collar.local` |
| Station connect timeout | 10 seconds |

AP name/password/mDNS are runtime configuration. Station credentials are a separate persisted record.

## Boot behavior

1. Reset the radio to OFF without framework credential persistence and wait 200 ms.
2. Register the event callback and load the project's CRC-protected station credential record.
3. If credentials exist, try to start AP directly in AP+STA mode (up to three boot attempts), then begin station connection.
4. Without credentials, start AP-only with the same bounded boot retry.
5. If AP start still fails, continue running and expose serial/diagnostic failure evidence; runtime retry is scheduled rather than attempted every loop.

`DEBUG_AP_ONLY_MINIMAL` is a compile-time isolation build that starts AP + portal only.

## Station behavior

- A successful station connection starts mDNS and records IP/RSSI/events.
- A 10-second connect timeout preserves/starts AP fallback and schedules station retry.
- Retries use exponential/backoff scheduling up to five minutes and never run from every loop iteration.
- A connected AP client delays station retry to avoid disrupting the setup session.
- Wi-Fi callbacks only enqueue events. The main-loop owner drains them and reconciles driver state on queue overflow.

Station SSIDs support 1–32 bytes. Passwords may be empty for an open network; validation intentionally differs from the collar's own WPA AP rules.

## AP availability policy

| Condition | Result |
| --- | --- |
| No trusted GNSS fix | AP is forced on and idle time is refreshed |
| Trusted fix at `<=2.0 km/h` for about 2 minutes | AP is requested on |
| Speed reaches `>=2.5 km/h` | Stationary accumulation resets |
| AP just started/restarted | 15-minute hold prevents immediate idle shutdown |
| Portal request received | Five-minute hold extends availability |
| AP has a client | Last-client activity is refreshed |
| Hold expired and no client for 10 minutes | SoftAP stops |
| AP stops while station credentials exist | Radio remains in station mode |
| AP stops without station credentials | SoftAP stops, but current policy deliberately does not force the whole Wi-Fi subsystem OFF; this keeps recovery/retry possible |

The code retains a Wi-Fi-OFF state and corresponding LED/homogeneous-mode behavior for explicit/future power control, but automatic AP idle shutdown no longer enters it. Documentation that says “idle AP always powers down the radio” is historical.

AP client count is maintained from connect/disconnect events with a slower 60-second driver reconciliation. This avoids synchronous radio queries in the hot loop while still recovering from drift.

## On-demand network scan

The `/wifi` page starts a scan only on user request because channel hopping can disturb the AP session.

1. `POST /api/wifi/scan` requests the asynchronous scan.
2. `GET /api/wifi/scan` reports `idle`, `scanning`, `failed`, or `ready`.
3. A ready response returns up to 20 unique, visible networks with RSSI/open state and reports the original total.
4. The ready response releases the driver result buffer.

Open-network selection is explicitly warned in the UI. Hidden SSIDs can still be typed manually.

## Captive portal

While AP is active, a wildcard DNS server maps queried names to the AP address. Common OS probes are served:

- `/generate_204` and `/gen_204`;
- `/hotspot-detect.html`;
- `/library/test/success.html`;
- `/ncsi.txt` and `/connecttest.txt`.

Unknown page paths redirect relatively to `/`; API paths return JSON 404/405 instead of dashboard HTML. Captive portal auto-launch varies by phone, so `http://192.168.4.1/` remains the documented fallback.

## AP configuration

- AP SSID: 1–32 bytes.
- Protected AP password: 8–63 characters.
- Open AP: explicit opt-in that persists an empty password and presents a warning.
- mDNS: 1–32 ASCII letters/digits/hyphens.
- Changing AP SSID/password schedules a restart after the response and disconnects the current AP client.

The config read API exposes only `has_ap_pass`/`has_sta_pass`, never password content.

## Diagnostics

`GET /api/dev` exposes current mode/address/channel/client state, credential A/B slot/generation/failures, AP start/stop/restart counters, retry schedules/remaining delays, event-queue high water/overflow, station events/failures, hold time, last transition reasons, DNS counters, and maximum radio-query timing.

Use those fields instead of assuming that a missing SSID means a single cause.

## Security boundary

The portal is HTTP on a local network. The AP password limits casual network access; optional PIN + CSRF intent header protect writes. Read-only telemetry and diagnostics are visible to connected clients. See [Local HTTP API](api-reference.md#write-guards) for the exact guard contract.
