# Phase 0 configuration and telemetry field matrix

**Status:** Current firmware inventory plus accepted future sync policy, verified 2026-08-13. No cloud field in this matrix is implemented.

This is the authoritative Phase 0 inventory requested before schema work. It distinguishes what the collar has **today** from what may cross the optional cloud boundary later. Current local behavior and validation come from `RuntimeConfig` schema 6, GNSS/session/route v2 code, and the documented local APIs.

## Reading the matrix

Privacy classes:

- **S0 public/product:** capability or non-personal presentation setting;
- **S1 private:** dog/collar operational or behavioral data;
- **S2 sensitive:** precise location, time/routine, Home, network/device identifiers;
- **S3 secret/safety:** password, PIN, device credential, or calibration whose remote change can affect electrical safety.

Sync policies:

- **BIDI `<resource>`:** future resource-level LWW sync after its named phase; collar remains capable offline;
- **UPLOAD v3:** append-only device observation/summary upload after v3/outbox work;
- **LOCAL:** never sent in the first full cloud release;
- **HEALTH-LATER:** not uploaded initially; a later allowlisted aggregate may be proposed, never a raw debug dump;
- **LEGACY-ONLY:** current v2 stays local/exportable; any explicit migration is marked legacy and cannot invent missing fields.

“Collar” as source means the local CRC/A/B store or live GNSS state is authoritative until a cloud resource is actually accepted. A future BIDI resource uses the canonical LWW winner as desired state and the collar's durable report as applied state; downloaded does not mean applied.

## `RuntimeConfig` schema 6 — every field

The indexed rows below explicitly cover every array element named in the field cell; there are no omitted implied fields.

| Firmware field(s) | Local API field | Type / unit | Current validation and default | Privacy | Current source of truth | Accepted first-release sync policy |
| --- | --- | --- | --- | --- | --- | --- |
| `brightness` | `led.brightness` | `uint8`, logical level | `1..255`; default `77` | S0 | collar config A/B | BIDI `brightness`; first vertical slice |
| `led_power_limit_enabled` | `led.power.enabled` | boolean | boolean; default `true` | S3 safety | collar physical calibration/config A/B | LOCAL |
| `led_power_budget_ma` | `led.power.budget_ma` | `uint16`, mA estimate | `250..5000`; default `1000` | S3 safety | collar physical calibration/config A/B | LOCAL |
| `led_base_current_ma` | `led.power.base_current_ma` | `uint16`, mA estimate | `0..1500` and `< budget`; default `200` | S3 safety | collar physical calibration/config A/B | LOCAL |
| `led_rgb_channel_ma` | `led.power.rgb_channel_ma` | `uint8`, mA per full R/G/B channel | `1..40`; default `20` | S3 safety | collar physical calibration/config A/B | LOCAL |
| `led_white_channel_ma` | `led.power.white_channel_ma` | `uint8`, mA per full W channel | `1..40`; default `20` | S3 safety | collar physical calibration/config A/B | LOCAL |
| `ranges[0]`, `[1]`, `[2]`, `[3]`, `[4]`, `[5]`, `[6]`, `[7]`, `[8]` | `speed_ranges_kph[0..8]` | nine `float`, km/h | exactly 9, each `>0`, strictly increasing; firmware has no explicit upper bound; defaults `2,4,6,8,10,12,14,16,18` | S0 | collar config A/B | BIDI `speed_profile` as all nine values with effects; not first slice |
| `effects[0].effect_a` … `effects[9].effect_a` | `effects.range1.a` … `range10.a` | ten `uint8` stable effect IDs | each `0..11`; all default `7` | S0 | collar config A/B + effect registry | BIDI `speed_profile`, all ten records atomically |
| `effects[0].effect_b` … `effects[9].effect_b` | `effects.range1.b` … `range10.b` | ten `uint8` stable effect IDs | each `0..11`; all default `7` | S0 | collar config A/B + effect registry | BIDI `speed_profile`, all ten records atomically |
| `effects[0].speed` … `effects[9].speed` | `effects.range1.speed` … `range10.speed` | ten `uint8`, effect-specific logical control | `0..255`; defaults `40,58,76,94,112,130,148,166,184,200` | S0 | collar config A/B | BIDI `speed_profile`, all ten records atomically |
| `effects[0].intensity` … `effects[9].intensity` | `effects.range1.intensity` … `range10.intensity` | ten `uint8`, effect-specific logical control | `0..255`; defaults `80,95,110,125,140,155,170,180,190,200` | S0 | collar config A/B | BIDI `speed_profile`, all ten records atomically |
| `single.effect_id` | `single.effect` | `uint8` stable effect ID | registry ID `0..11`; default `0` | S0 | collar config A/B + effect registry | BIDI `simple_effect` atomically |
| `single.speed` | `single.speed` | `uint8`, effect-specific logical control | `0..255`; default `80` | S0 | collar config A/B | BIDI `simple_effect` atomically |
| `single.intensity` | `single.intensity` | `uint8`, effect-specific logical control | `0..255`; default `140` | S0 | collar config A/B | BIDI `simple_effect` atomically |
| `single.base_r` | `single.rgb.r` | `uint8`, red logical channel | `0..255`; default `0` | S0 | collar config A/B | BIDI `simple_effect` atomically |
| `single.base_g` | `single.rgb.g` | `uint8`, green logical channel | `0..255`; default `60` | S0 | collar config A/B | BIDI `simple_effect` atomically |
| `single.base_b` | `single.rgb.b` | `uint8`, blue logical channel | `0..255`; default `60` | S0 | collar config A/B | BIDI `simple_effect` atomically |
| `ap_ssid` | `wifi.ap_ssid` | UTF-8/string, bytes | 1–32 bytes, no controls or leading/trailing spaces; default `DogRGB` | S2 network identity | collar config A/B | LOCAL |
| `ap_pass` | write-only `wifi.ap_pass`; reads expose only `has_ap_pass` | secret string | empty only for explicit open AP; otherwise 8–63 printable characters; default `Dog12345` | S3 secret | collar config A/B | LOCAL; never cloud/log/read API |
| `mdns` | `wifi.mdns` | ASCII hostname label | 1–32 alphanumeric/hyphen, no leading/trailing hyphen; default `dog-collar` | S2 device/network identity | collar config A/B | LOCAL |
| `mode` | `mode` | `uint8` enum / JSON string | `speed`, `geofence`, `show`, `simple`; default `speed` | S0 | collar config A/B | BIDI `visual_mode` with Day Mode |
| `day_mode_enabled` | `day_mode.enabled` | boolean | boolean; default `false`; fixed window/timezone are compile-time, not fields | S1 routine inference | collar config A/B | BIDI `visual_mode` with mode |
| `fence_max_m` | `fence_max_m` | `uint16`, meters | `50..5000`; default `300` | S1 | collar config A/B | BIDI `geofence_policy`; Home stays LOCAL |
| `gps_min_fix_quality` | `gps.min_fix_quality` | `uint8`, NMEA GGA quality code threshold | `0..8`; default `1` | S1 quality policy | collar config A/B | BIDI `gps_quality` expert resource |
| `gps_min_sats` | `gps.min_sats` | `uint8`, satellites | `3..12`; default `6` | S1 quality policy | collar config A/B | BIDI `gps_quality` expert resource |
| `gps_max_hdop` | `gps.max_hdop` | `float`, dimensionless HDOP | `0.5..20.0`; default `2.5` | S1 quality policy | collar config A/B | BIDI `gps_quality` expert resource |
| `gps_max_gga_age_ms` | `gps.max_gga_age_ms` | `uint16`, ms | `500..10000`; default `2000` | S1 quality policy | collar config A/B | BIDI `gps_quality` expert resource |
| `gps_min_segment_m` | `gps.min_segment_m` | `float`, meters | `0.5..20.0`; default `3.0` | S1 metric policy | collar config A/B | BIDI `gps_quality` expert resource |
| `gps_hdop_factor` | `gps.hdop_factor` | `float`, meters per HDOP unit | `0.0..5.0`; default `2.0` | S1 metric policy | collar config A/B | BIDI `gps_quality` expert resource |
| `gps_max_min_segment_m` | `gps.max_min_segment_m` | `float`, meters | `1.0..50.0`, `>= gps_min_segment_m`; default `10.0` | S1 metric policy | collar config A/B | BIDI `gps_quality` expert resource |

Fields adjacent to but **not inside** `RuntimeConfig` are still governed explicitly:

| Separate local record/state | Unit/range | Privacy | First-release sync |
| --- | --- | --- | --- |
| station Wi-Fi SSID/password | SSID 1–32 bytes; password empty or `1..63` printable characters (legacy short values accepted, raw 64-hex PSK rejected) | S2/S3 | LOCAL; never upload |
| Home set/source/latitude/longitude | boolean/enum, WGS84 degrees | S2 precise Home | LOCAL by explicit ADR decision |
| local portal PIN | 4–8 ASCII digits; write-only | S3 | LOCAL; never upload |
| scene bank/active override | bounded versioned recipes; no secrets/location | S0 | LOCAL in first cloud release; separate future decision |
| cloud opt-in/device credential/claim/revoke state | versioned state; 256-bit secret plus credential UUID; durable `REVOKE_PENDING` request ID/reason | S3 | plaintext persists on collar, crosses only verified TLS in claim body or sync/revoke Authorization, and is never part of config/history/read/log/persistent server data; server stores HMAC digest. Normal unlink clears locally only after a schema-valid matching `device-v1-revoke` `200` disposition `newly_revoked|already_revoked`; exact replay returns the original result and generic errors retain state. Offline force-clear warns that website revoke remains required |
| Day Mode start/end/timezone | compile-time `360`, `960`, `-300` minutes today | S1 | not mutable/synced; future product timezone lives per dog, not this constant |

## Current daily and session telemetry

`/api/summary` is a local presentation contract. It is not a complete raw cloud history contract.

| Current field | Type / unit and range | Privacy | Collar source/meaning | Cloud policy |
| --- | --- | --- | --- | --- |
| `date` | integer `YYYYMMDD` or `0` | S1 routine | current accepted GNSS date accumulator | UPLOAD summary after pairing, source-labelled; cloud local-day is derived separately |
| `distance_m` | rounded `uint32`, meters | S1 activity | accepted Haversine segments for current firmware day | UPLOAD device-reported summary; never overwrite cloud-derived distance |
| `avg_speed_cmps` | `uint16`, cm/s | S1 activity | `distance / active_time`, clamped; not sample mean | UPLOAD device-reported summary with definition/version |
| `max_speed_cmps` | `uint16`, cm/s | S1 activity | max admitted speed, clamped; >40 km/h inputs rejected | UPLOAD device-reported summary with definition/version |
| `last_update_min` | `uint16`, UTC minute `0..1439` | S2 time/routine | minute of last accepted dated observation | UPLOAD with date/time-quality context; not sufficient for seconds |
| `gps_fix` | boolean | S1 quality | current trusted fix | live/transient LOCAL; v3 point flags carry historical evidence |
| `gps_raw_fix` | boolean | S1 quality | raw RMC valid-fix indication before GGA quality gate | live/transient LOCAL; v3 flags/quality replace for history |
| `gps_quality_ok` | boolean | S1 quality | current GGA freshness/fix/sats/HDOP gate | live/transient LOCAL; v3 flags carry per-observation result |
| `has_data` | boolean | S1 | whether current date is nonzero, not coverage | summary upload allowed; UI must not equate with full-day coverage |
| `last_completed_day` | nullable object | S1/S2 | CRC A/B daily rollover journal | UPLOAD summary after pairing; preserve as device-reported |
| `last_completed_day.date` | `YYYYMMDD` | S2 routine | completed firmware GNSS date | same as parent |
| `last_completed_day.distance_m` | float serialized to 0.1 m | S1 | accepted device distance | same as parent |
| `last_completed_day.active_ms` | `uint32`, ms | S1 | device active-time rule | same as parent; do not label exercise truth |
| `last_completed_day.max_speed_kph` | float serialized to 0.1 km/h | S1 | device maximum admitted speed | same as parent |
| `last_completed_day.last_update_min` | minute `0..1439` | S2 routine | last accepted update minute | same as parent |
| `history` | array of 0–3 session summaries | S1/S2 | newest completed boot recordings | LEGACY-ONLY/current local; future stable recording identities must replace slot identity |
| `session_current` | nullable session summary | S1/S2 | current boot recording snapshot | LEGACY-ONLY/current local; not a detected walk |
| session `start_date`, `end_date` | each `YYYYMMDD` or zero for no-fix record | S2 routine | accepted GNSS date boundaries | future recording import/upload with explicit source limits |
| session `start_min`, `end_min` | each UTC minute `0..1439` | S2 routine | minute boundaries only | same; no invented seconds |
| session `distance_m` | `uint32`, meters | S1 activity | accepted session segments | same; device-reported |
| session `active_s` | `uint32`, seconds | S1 activity | device active intervals | same; not walk/exercise truth |
| session `avg_speed_cmps` | `uint16`, cm/s | S1 activity | integer `distance_m*100/active_s`, clamped | same |
| session `max_speed_cmps` | `uint16`, cm/s | S1 activity | session maximum admitted speed | same |
| session `flags` | `uint8` mask: `0x01 GPS_FIX`, `0x02 HAS_DATA`, `0x04 IN_PROGRESS`, `0x08 NO_FIX`; no other bits valid | S1 quality | session record status/evidence | same; translate to named booleans at cloud boundary rather than expose opaque bits |

Current firmware sessions are boot-to-reboot recordings. Neither `history` nor `session_current` may be renamed “walk.”

## Current route v2 telemetry

| Firmware/current API field | Type / unit and range | Privacy | Source/limitation | Cloud policy |
| --- | --- | --- | --- | --- |
| `TrackPoint.lat_e7` / JSON point latitude | `int32`, degrees × 1e7; valid WGS84 `[-900000000,900000000]` | S2 precise location | accepted, moving-only route sample | LEGACY-ONLY; explicit conversion may set `LEGACY_V2` |
| `TrackPoint.lon_e7` / JSON point longitude | `int32`, degrees × 1e7; valid WGS84 `[-1800000000,1800000000]` | S2 precise location | accepted, moving-only route sample | LEGACY-ONLY |
| `TrackPoint.t_min` / CSV `minute_utc` | `uint16`, minute `0..1439` | S2 precise time | minute only; JSON/GeoJSON coordinate arrays omit it | LEGACY-ONLY; cannot reconstruct seconds/order across ambiguity |
| route `count` | `uint16`, `0..1440` exported points | S1 | bounded snapshot count | local metadata; cloud derives count from accepted points |
| route `open` | boolean | S1 | whether selected slot is current | local slot state; future recording `is_final` uses stable identity |
| route `sample_ms` | `uint16`, nominal `5000` ms | S1 | configured nominal capture cadence, not proof of coverage | preserve as legacy metadata only |
| route `start_date`, `end_date` | `YYYYMMDD` | S2 routine | slot boundary date | legacy metadata only |
| route `start_min`, `end_min` | UTC minute `0..1439` | S2 routine | slot boundary minute | legacy metadata only |
| bbox `min_lat`, `max_lat`, `min_lon`, `max_lon` | decimal WGS84 degrees | S2 precise location | recomputed snapshot bounds | do not upload/log as separate v3 truth; derive after authorization |
| `points` / GeoJSON coordinates | ordered coordinate pairs; JSON `[lat,lon]`, GeoJSON `[lon,lat]` | S2 precise location | no seconds, speed, satellites, quality, stationary samples, sequence, or explicit gap | LEGACY-ONLY; never infer missing fields |
| CSV `date` | `YYYYMMDD` | S2 | derived from route boundary/point minute logic | legacy only |

Current route capture is called only for a speed-qualified active sample. It therefore cannot measure inactivity, stationary coverage, or full-day wearing. A five-second nominal interval does not permit joining missing points across resets/fix loss.

## Live GNSS/metric fields and diagnostics

These rows cover current GPS getters and the GPS parts of `/api/status` and `/api/dev`. Except for fields represented in future v3 points/summaries, they remain local support data. An Internet sync must not copy `/api/dev` wholesale.

| Current field(s) | Unit/range/sentinel | Privacy | Source/meaning | Cloud policy |
| --- | --- | --- | --- | --- |
| `fix`, `raw_fix`, `trusted_fix`, `quality_ok`, `current_fix`, `speed_usable` | booleans | S1 | current RMC/GGA trust and availability state | LOCAL live; historical equivalents only through exact v3 flags |
| `sats` | `uint8`, satellites `0..255` storage range | S1 quality | latest parsed GGA count | UPLOAD v3 per point when collected |
| `fix_quality` | `uint8`, GGA quality `0..8` expected | S1 quality | latest parsed GGA code | HEALTH-LATER; v3 initial point uses validity/quality flags, not raw code |
| `hdop` | positive float, API `-1` unavailable | S1 quality | latest parsed GGA HDOP | HEALTH-LATER aggregate; current v3 fixed point does not carry HDOP |
| `speed_kph` / `last_speed_kph` | float km/h, admitted `0..40`; `0` when unusable | S1 activity | latest trusted RMC speed | v3 uses `speed_cmps`, `65535` unavailable; never infer for legacy |
| `lat`, `lon`, `current_lat_deg`, `current_lon_deg` | WGS84 degrees | S2 precise location | latest raw-valid current coordinate, not necessarily trusted | no standalone live upload; v3 point only under opt-in and validity rules |
| `last_lat_deg`, `last_lon_deg`, `has_last_point` | WGS84 degrees/boolean | S2 precise location | internal distance baseline | LOCAL; never log/upload as diagnostics |
| `date` / `current_date` | `YYYYMMDD` or 0 | S2 routine | current accepted GNSS date | summary/v3 time only with quality |
| `last_update_min` | UTC minute `0..1439` | S2 routine | accepted GNSS minute | summary/legacy metadata as specified above |
| `has_time`, `local_time_min`, `last_time_ms` | boolean; local minute `0..1439`/API `-1`; boot-relative ms | S2 routine | freshness and fixed `-300` Day Mode conversion | LOCAL; future cloud uses UTC/time quality + per-dog IANA timezone |
| `total_distance_m` | float meters, nonnegative until accumulator/type limit | S1 activity | current accepted daily distance | UPLOAD only as device summary, not raw diagnostic |
| `max_speed_kph` | float km/h, `0..40` admitted | S1 activity | current device-day maximum | UPLOAD only as device summary |
| `active_time_ms` | 32-bit boot/platform unsigned milliseconds | S1 activity | accepted adjacent active interval sum | UPLOAD only as source-labelled device summary |
| `activity_observation_intervals`, `activity_gap_rejects` | `uint32` counters | S1 quality | current-day activity interval admission/rejection | HEALTH-LATER aggregate if required for discrepancy support |
| `last_activity_delta_ms` | `uint32`, ms | S1 quality | most recent evaluated interval | LOCAL |
| `date_transitions`, `date_rejected` | `uint32` counters | S1 quality | GNSS date trust guard outcomes | HEALTH-LATER aggregate |
| `date_pending_candidate` | `YYYYMMDD` or 0 | S2 routine | unaccepted forward-date candidate | LOCAL |
| `date_pending_observations` | `uint8` counter | S1 quality | repeated evidence for pending date | LOCAL |
| `bytes_rx`, `sentences_rx`, `rmc_seen`, `rmc_valid`, `gga_seen`, `overflow` | ESP32 `unsigned long` counters, `0..2^32-1`, wrap possible | S1 device health | since-boot UART/parser counters | HEALTH-LATER allowlisted aggregate only |
| `checksum_fail`, `parse_fail`, `rmc_parse_fail`, `gga_parse_fail` | 32-bit since-boot counters | S1 device health | parser failures | HEALTH-LATER allowlisted aggregate only |
| `speed_spike`, `small_segment_rejects`, `large_segment_rejects`, `stale_count` | 32-bit since-boot counters | S1 quality/activity | filter rejection counters | HEALTH-LATER allowlisted aggregate only |
| `has_byte_observation`, `has_rmc_observation`, `has_gga_observation`, `has_fix_observation` | booleans | S1 device health | whether each observation type has occurred since boot | LOCAL or compact HEALTH-LATER capability/status only |
| `last_byte_ms`, `last_rmc_ms`, `last_gga_ms`, `last_fix_ms` | boot-relative `millis()` value | S1 | last occurrence; wraps | LOCAL; gateway may send bounded ages instead if health feature approved |
| `age_last_byte_ms`, `age_last_fix_ms`, `age_last_gga_ms` | signed API number, `-1` never; otherwise age ms | S1 | wrap-safe derived age at request | HEALTH-LATER aggregate only |
| `last_segment_m` | float meters | S1 activity/quality | last evaluated Haversine segment | LOCAL |
| `last_segment_accepted` | boolean | S1 quality | current segment admission | LOCAL |
| `last_segment_reject_reason` | enum-like string: current values include `none`, `ok`, `baseline`, `inactive_speed`, `bad_fix`, `speed_spike`, `date_pending`, `small_segment`, `large_segment` | S1 quality | diagnostic outcome | HEALTH-LATER bounded code only if required |
| metrics storage `slot`, `generation`, `save_failures`, `recoveries` | slot `-1/0/1`; `uint32` counters/generation | S1 device health | current metric A/B store health | HEALTH-LATER aggregate |
| session storage `slot`, `generation`, `save_failures`, `recoveries`, `history_count` | slot `-1/0/1`; `uint32`; count `0..3` | S1 device health | current session store health | HEALTH-LATER aggregate |
| daily journal `slot`, `generation`, `save_failures`, `last_completed_date` | slot `-1/0/1`; `uint32`; `YYYYMMDD/0` | S1/S2 | completed-day journal health/date | summary field may upload; storage mechanics HEALTH-LATER/local |

## Other current status/developer telemetry

These fields are observable locally but are not dog historical data. They are inventoried so implementation cannot accidentally treat “send diagnostics” as permission to upload them.

| Current group / exact fields | Unit/range | Privacy | Source | Sync policy |
| --- | --- | --- | --- | --- |
| time/system: `uptime_ms`, `build`, `free_heap`; config storage `slot`, `generation`, `save_failures` | boot ms/build string/bytes/counters | S1 fingerprint/health | ESP/config store | HEALTH-LATER allowlist only; build/capability version may accompany sync, raw heap timeline does not |
| serial `[SYS]`: `uptime_s`, `heap`, `min_heap`, `loop_avg_us`, `loop_max_us`, `loop_work_avg_us`, `loop_work_max_us`, `log_emit_max_us`, `log_drain_max_us`, `log_queue_pending`, `log_drop_bytes`, `log_slot0_max_us`…`log_slot6_max_us`, `gps_max_us`, `control_max_us`, `geofence_max_us`, `storage_max_us`, `radio_max_us`, `led_max_us`, `http_max_us` (plus repeated mode/brightness/date/Day Mode fields) | seconds/bytes/microseconds/counts and already-inventoried config/routine values; rolling periodic diagnostic maxima/averages, not `/api/dev` fields | S1 device health/fingerprint; date/routine S2 | serial-only periodic `[SYS]` log and in-memory loop/log/phase accumulators | LOCAL; never bulk-upload serial logs. Any future bounded health aggregate needs an explicit allowlist, privacy review, and schema; derived delta/anomaly values inherit the same policy |
| serial `[GPS_LINK]`: `uart`, `bytes_delta`, `nmea_delta`, `rmc_delta`, `gga_delta`, `checksum_fail_delta`, `parse_fail_delta`, `rmc_parse_fail_delta`, `gga_parse_fail_delta`, `speed_spike_delta`, `overflow`, `stale_delta`, `age_byte_ms`, `age_rmc_ms`, `age_gga_ms`; `[GPS_ANOMALY]`: `checksum_seen`, `parse_seen`, `rmc_parse_seen`, `gga_parse_seen`, `speed_spike_seen`, `stale_seen`, `overflow` | booleans, counter deltas, and ages in ms (`-1` means never) | S1 device health | serial-only views derived from already-inventoried GNSS counters/timestamps | LOCAL; never bulk-upload serial logs; any future compact health sample uses an explicit allowlist |
| serial `[GPS_FIX]`: `raw`, `trusted`, `current`, `reason`, `fixq`, `sats`, `hdop`, `fix_age_ms`, `lat`, `lon` | booleans/bounded reason/GGA code/count/dimensionless/ms/WGS84 | S1 quality plus S2 precise location | serial-only view derived from current GNSS state | LOCAL; coordinates never upload as diagnostics or enter logs outside the physical serial console |
| serial `[MOTION]`: `mode`, `speed_kph`, `usable`, `active`, `range`, `dist_m`, `active_s`, `avg_kph`, `max_kph`, `seg_m`, `seg_ok`, `seg_reason`, `small_seg_total`, `large_seg_total`; `[LED]`: `mode`, `body_on`, `render`, `home_missing`, `geofence_dist_m`, `range`, `effect_a`, `effect_b`, `speed`, `intensity`, `day_mode`, `local_min` | already-inventoried enums/booleans/km/h/meters/seconds/counters/effect controls/minute | S0/S1; geofence relation and routine S2 | serial-only derived/repeated motion, geofence, LED, and Day Mode views | LOCAL; no live state stream; future aggregate only through a separate allowlisted schema |
| serial `[WIFI]`: `mode`, `sta`, `sta_try`, `ap`, `clients`, `wifi_off`, `ssid_set`, `rssi`, `ap_ip`, `sta_ip`, `ap_ch`; `[WIFI_DIAG]`: `ap_start`, `ap_fail`, `ap_stop`, `ap_restart`, `sta_retry`, `sta_fail`, `sta_got_ip`, `sta_disc`, `ap_conn`, `ap_disc`, `ap_hold_s`, `retry_s`, `last_ap_rsn`, `last_sta_rsn`, `ap_poll_max_us`, `channel_query_max_us` | enums/booleans/counts/dBm/IP/channel/seconds/reasons/microseconds; `-1` unavailable timers | S1/S2 network/device presence | serial-only derived aliases of already-inventoried Wi-Fi state/diagnostics | LOCAL; never upload SSID/IP/radio identity in the first release |
| Wi-Fi state: `mode`, `sta_connected`, `sta_connecting`, `ap_enabled`, `ap_stations`, `wifi_off` | enum/booleans/count | S1/S2 presence | Wi-Fi manager | LOCAL; a future compact connection result may expose no SSID/MAC/IP |
| Wi-Fi identity/network: `ap_ssid`, `mdns`, `sta_ip`, `ap_ip`, `rssi`, `ap_mac`, `sta_mac` | strings/IP/dBm/MAC | S2 network/device identity | Wi-Fi stack/config | LOCAL; never upload in first release |
| Wi-Fi storage/diagnostic fields: `storage.slot`, `storage.generation`, `storage.save_failures`, `last_ap_start_ok`, `ap_start_count`, `ap_start_fail_count`, `ap_retry_schedule_count`, `ap_stop_count`, `ap_restart_count`, `sta_retry_count`, `sta_connect_fail_count`, `ap_station_connect_count`, `ap_station_disconnect_count`, `sta_got_ip_count`, `sta_disconnect_count`, `event_queue_overflow_count`, `event_queue_high_water`, `last_ap_start_ms`, `last_ap_stop_ms`, `next_ap_retry_ms`, `ap_retry_delay_ms`, `ap_retry_scheduled`, `ap_retry_remaining_ms`, `last_sta_retry_ms`, `next_sta_retry_ms`, `sta_retry_scheduled`, `sta_retry_remaining_ms`, `ap_hold_until_ms`, `ap_hold_scheduled`, `ap_hold_remaining_ms`, `last_wifi_event_ms`, `last_wifi_event`, `current_ap_channel`, `ap_station_poll_max_us`, `channel_query_max_us`, `last_ap_reason`, `last_ap_failure_stage`, `last_sta_reason`, `dns_running`, `dns_start_count`, `dns_stop_count` | booleans/32-bit counts or boot-relative ms/us/enums/channel | S1/S2 | Wi-Fi manager/portal DNS | LOCAL; later aggregate health requires a separate allowlist/privacy review |
| Home/geofence status: `set`, `source`, `home_lat`, `home_lon`, storage `slot/generation/save_failures`, `distance_m`, `range` | boolean/enum/WGS84/meter/range index; unavailable `-1` | S2 precise Home/current relationship | Home A/B + current coordinate | LOCAL by accepted first-release decision |
| `/api/v1/led/state`: `schema_version`, `mode`, `intent`, `priority`, `body_enabled`, `status_enabled`, `homogeneous`, `mirror`, `alert`, `critical_alert`, `range`, `brightness`, `body_level`, `transition_ms`, `base_rgb.{r,g,b}`, `accent_rgb.{r,g,b}`; `effect_a` and `effect_b` each expose `{id,key,name,speed,intensity,palette_id,palette_key,palette_name}`; `scene.{id,key,name,origin,playback,pending_id,stale,applied_generation,activation_revision,store_generation}` | schema/version, bounded enums/strings/booleans/IDs, logical levels/channels `0..255`, ms and generations | S0/S1 | selected runtime policy, registries, scene player/store | configuration resources only where explicitly listed; no live LED telemetry upload |
| LED power: config fields plus `requested_ma`, `estimated_ma`, `peak_requested_ma`, `scale`, `scale_percent`, `frames_limited`, `estimate_only` | estimated mA, scale `0..255`, percent, counter | S3 calibration / S1 health | software model, **not sensor** | LOCAL; never imply measured battery/current |
| LED transition: `active`, `duration_ms`, `progress`, `started`, `completed`, `interrupted` | boolean/ms/scale/counters | S0/S1 | compositor diagnostics | LOCAL |
| scene diagnostics: `store.{health,generation,read_only}`, `active.{id,key,name,origin,playback,pending_id,stale,applied_generation,activation_revision}`, `bank_a`, `bank_b`, `active_bank`, `bank_a_generation`, `bank_b_generation`, `free_entries`, `load_count`, `recovery_count`, `mutation_count`, `write_failures`, `verify_failures`, `uncertain_commits`, `last_write_us`, `max_write_us`, `last_save_us`, `max_save_us`, `last_import_us`, `max_import_us`, `max_led_gap_during_write_us`, `apply_count`, `cancel_count`, `superseded_commands`, `show_cycle_count`, `lookup_failures` | bounded health/status enums, strings/IDs, booleans, generations/counters/us | S1 health/config | scene store/player/runtime | LOCAL; later aggregate health only by explicit schema |
| Day Mode status: `enabled`, `active`, `state`, `time_available`, `local_min`, `start_min`, `end_min`, `tz_offset_min` | boolean/enum/minutes | S1 routine | runtime config + GNSS time + compile-time window | only `enabled` in BIDI `visual_mode`; live state/time remains LOCAL |

## Frozen v3 target fields (not current firmware)

The Phase 0 fixed codec in [`tools/cloud_phase0/track_v3.py`](../../tools/cloud_phase0/track_v3.py) is the storage feasibility target. The JSON/device protocol must match it byte-for-byte/semantically before Phase 0 closes; any current contract disagreement is a blocker, not permission to use two meanings.

| V3 field | Canonical type/unit/range | Privacy/source | Sync policy |
| --- | --- | --- | --- |
| `lat_e7`, `lon_e7` | signed 32-bit WGS84 ×1e7; must be zero when `FIX_VALID=0` | S2 collar observation | UPLOAD v3 |
| `utc_s` | `uint32` Unix seconds; `0` exactly when `TIME_TRUSTED=0` | S2 collar time evidence | UPLOAD v3 |
| `speed_cmps` | `uint16` cm/s; `65535` unavailable and mandatory without valid fix | S1 collar observation | UPLOAD v3 |
| `satellites` | `uint8` count | S1 quality | UPLOAD v3 |
| point `flags` | bitmask: `0x01 FIX_VALID`, `0x02 MOVEMENT_EVIDENCE`, `0x04 TIME_TRUSTED`, `0x08 STATIONARY_HEARTBEAT`, `0x10 LOW_QUALITY`, `0x20 GAP`, `0x40 LEGACY_V2`; `0x80` reserved/invalid | S1 quality/evidence | UPLOAD v3; exact invariants below |
| `device_id` | non-nil UUID, 16 canonical bytes | S2 linkable device identity | authenticated chunk identity; server cross-checks credential |
| `boot_sequence`, `chunk_sequence`, `first_point_sequence` | `uint32`; boot `0` reserved for legacy conversion; point range cannot overflow | S1 integrity | UPLOAD v3/idempotency/order |
| `point_count` | `1..96`, equal payload count | S1 integrity | UPLOAD v3 |
| `time_quality` | `0 unknown`, `1 approximate_persisted`, `2 server_anchored`, `3 sntp_synced`, `4 gnss_trusted`, `5 legacy_minute` | S1 quality; exact source/precision evidence | UPLOAD v3; only values 2/3/4 may author config HLC within ±600 seconds; 0/1/5 rebase |
| `final_for_recording` / chunk flag `0x0001` | boolean; all other chunk flag bits invalid | S1 recording state | UPLOAD v3 |
| `start_utc_s`, `end_utc_s` | derived `uint32` nonzero point bounds or both zero; timestamps monotonic within chunk | S2 time/routine | UPLOAD v3/header validation |
| `payload_len`, `payload_crc32`, `payload_sha256`, header CRC | exact fixed-codec length/CRC-32/SHA-256/integrity fields | S1 integrity; hash is not a secret | transport/storage verification; database/request hash uses canonical definition |

Device-v1 may also carry an advisory, source-versioned summary and an explicit loss marker. Neither can replace raw point truth:

| V3 advisory field | Canonical type/unit/range | Privacy/source | Sync policy |
| --- | --- | --- | --- |
| `summary_id` | UUID v4 | S1 stable summary identity | UPLOAD v3/idempotency |
| `local_date`, `timezone` | ISO local date plus IANA zone name | S2 routine | UPLOAD v3; day boundary/provenance, not a fixed UTC offset |
| `source_revision` | `uint32` | S1 algorithm provenance | UPLOAD v3; device and cloud revisions remain distinct |
| `window_start`, `window_end` | UTC RFC 3339 instants; end strictly after start | S2 routine | UPLOAD v3; bound the advisory summary |
| `observed_s`, `moving_s`, `inactive_s` | `uint32` seconds; `moving_s + inactive_s == observed_s`, and observed cannot exceed the window | S1 activity/coverage | UPLOAD v3; `inactive_s` means only observed stationary/inactive time within usable coverage, never window-minus-moving or missing/off-collar time |
| summary `distance_m` | `uint32` meters | S1 activity | UPLOAD v3 as device-reported advisory value; cloud derivation remains separate |
| summary `max_speed_cmps` | `0..65534` cm/s or `null` unavailable | S1 activity | UPLOAD v3 as device-reported advisory value |
| `valid_points`, `gap_count`, `dropped_points` | `uint32` counts | S1 quality/coverage | UPLOAD v3; preserve zero versus unknown according to source revision |
| summary `time_quality` | `unknown`, `approximate_persisted`, `server_anchored`, `sntp_synced`, or `gnss_trusted` | S1 quality | UPLOAD v3; legacy-minute chunks do not manufacture a native daily-summary precision claim |
| `marker_id` | UUID v4 | S1 stable loss identity | UPLOAD v3/idempotency |
| loss `boot_sequence`, `first_missing_point_sequence`, `last_missing_point_sequence`, `lost_points` | `uint32`; inclusive range length equals `lost_points >= 1` | S1 integrity/coverage | UPLOAD v3; creates explicit unknown coverage and never acknowledges surrounding chunks |
| loss `reason` | `storage_pressure`, `corrupt_chunk`, `user_reset`, or `legacy_unavailable` | S1 operational provenance | UPLOAD v3; fixed bounded vocabulary |
| `recorded_utc_ms` | Unix milliseconds `0..4102444800000`, or `null` if unavailable | S2 time evidence | UPLOAD v3; `null` remains unknown, not ingestion time |

Point invariants are part of the field definition:

- `MOVEMENT_EVIDENCE` and `STATIONARY_HEARTBEAT` are mutually exclusive;
- `GAP` cannot also claim valid fix, movement, or stationary evidence;
- no valid fix means zero coordinates and unavailable speed;
- `TIME_TRUSTED` exactly matches nonzero `utc_s`;
- `LEGACY_V2` is valid only for the reserved legacy boot sequence and `legacy_minute` time quality;
- `(utc_s != 0) == TIME_TRUSTED == (time_quality != unknown)` for every point; each chunk uses exactly one quality and timestamps remain monotonic;
- flags are evidence, not dog-behavior labels. Stationary may mean the collar sat on a table.

The unfortunate wire name `inactive_s` is frozen for device-v1 compatibility. Product copy and database views must expose it as **observed stationary/inactive**, paired with `observed_s`/coverage. It is never eligible-day duration minus moving time, and gaps, power-off, fix loss, storage loss, or an unworn collar remain unknown.

## Decisions and Phase 0 gates

Accepted:

- cloud is opt-in; offline operation remains mandatory;
- Home, power calibration, AP/station credentials, mDNS, and PIN are collar-local in the first release; the device secret is excluded from synchronized data and persists only on the collar, while verified-TLS authentication necessarily presents it transiently to the Edge gateway;
- only `brightness` enters the initial bidirectional slice;
- v2 remains readable/exportable and is never silently promoted to v3 evidence;
- raw diagnostics are not uploaded by default;
- device and cloud summaries remain separate and source/version-labelled.
- `contracts/device-v1`, including dedicated revoke, is reconciled with the fixed v3 codec and passes protocol 48/48. The superseded RAM-only storage model's 20/20 result is invalid historical evidence. The corrected 664-slot byte-addressed candidate implements exact ACK/fresh-image mechanisms and now passes all seven reproduced adversarial regressions in its 51/51 suite; independent host acceptance remains open. Exact ACK/hole semantics remain frozen contract requirements.

Still blocking Phase 0 exit:

- the corrected host outbox candidate must keep every reproduced fallback/loss/corruption regression green and obtain independent recovery/reclaim acceptance;
- physical ESP32 outbox/power-cut/timing/wear/energy evidence remains open;
- full credentialed MapTiler/Stadia comparison, unapproved-origin proof, and two-reviewer score remain open;
- no web/database/firmware cloud implementation may be inferred from this inventory; local-only Phase 1 proceeds under the parent plan's explicit exception, and Phase 2 remains unauthorized.

## Current source references

- [`RuntimeConfig` reference](../portal_config.md)
- [Local HTTP API](../api-reference.md)
- [Architecture and current persistence](../architecture.md)
- [Truthful metric vocabulary](../adr/0010-retention-and-truthful-activity-vocabulary.md)
- [Storage feasibility report](phase0-storage-feasibility.md)
