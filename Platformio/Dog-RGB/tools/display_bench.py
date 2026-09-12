"""Optional serial capture for an already identified/flashed I3 bench device.

Only LCD commands are sent. This tool never flashes, resets, writes settings,
injects GPS data or declares whole-collar physical acceptance.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import time
from analyze_wokwi import FATAL_MARKERS, fields

COMMANDS = frozenset("tvbdfrslacn")


def summarize(text: str) -> dict:
    lcd, system, gps = [], [], []
    boot_lines = 0
    malformed_records = 0
    for line in text.splitlines():
        if "[BOOT] reset_reason=" in line:
            boot_lines += 1
        # A reconnect may join a dropped partial record to the next one. Do not
        # combine timestamps/counters from different reports into evidence.
        if line.startswith(("[LCD] ", "[SYS] ", "[I3] ")) and re.search(r"\[[A-Z][A-Z0-9_]*\]", line[1:]):
            malformed_records += 1
            continue
        if line.startswith("[LCD] "):
            lcd.append(fields(line))
        elif line.startswith("[SYS] "):
            system.append(fields(line))
        elif line.startswith("[I3] "):
            gps.append(fields(line))
    # Return observations, not a pass from an empty/static/fake dataset.
    def values(rows, key):
        return [r[key] for r in rows if isinstance(r.get(key), (int, float))]
    def span(rows, key):
        v = values(rows, key)
        return {"first": v[0], "last": v[-1], "min": min(v), "max": max(v)} if v else None
    uptime = values(system, "uptime_s")
    gps_ms = values(gps, "ms")
    # A 32-bit wrap is allowed; backward jumps smaller than half range are resets.
    resets = sum(a > b and a - b < 0x80000000 for a, b in zip(gps_ms, gps_ms[1:]))
    return {
        "scope": "serial observations only; LCD DEMO is not GNSS or physical LED evidence",
        "lcd_reports": len(lcd), "system_reports": len(system), "gps_reports": len(gps),
        "malformed_records": malformed_records,
        "observed_sys_span_s": uptime[-1] - uptime[0] if len(uptime) >= 2 else None,
        "boot_lines": boot_lines, "gps_clock_regressions": resets,
        "fatal_markers": [marker for marker in FATAL_MARKERS if marker in text.lower()],
        "lcd_states": [list(s) for s in sorted({(r.get("ready"), r.get("enabled"), r.get("light"),
                                               r.get("test"), r.get("demo", 0)) for r in lcd}, key=str)],
        "lcd_tick_max_us": max(values(lcd, "tick_max_us"), default=None),
        "lcd_draw_p95_upper_max_us": max(values(lcd, "p95_upper_us"), default=None),
        "lcd_last": lcd[-1] if lcd else None,
        "lcd_backends": sorted({str(r["ui"]) for r in lcd if "ui" in r}),
        "lcd_pages": sorted({str(r["page"]) for r in lcd if "page" in r}),
        "heap": span(system, "heap"), "min_heap": span(system, "min_heap"),
        "log_drop_bytes": span(system, "log_drop_bytes"),
        "gps_rx": span(gps, "rx"), "gps_overflow": span(gps, "overflow"),
        "gps_states": sorted({str(r.get("state")) for r in gps}),
        "missing": [name for name, rows in (("LCD", lcd), ("SYS", system), ("I3", gps)) if not rows],
    }


def parse_step(value: str) -> tuple[float, str]:
    try:
        when, command = value.split(":", 1)
        when = float(when)
        if not 0 <= when < float("inf") or len(command) != 1 or command not in COMMANDS:
            raise ValueError()
        return when, command
    except ValueError as exc:
        raise argparse.ArgumentTypeError("Use seconds:command, where command is t/v/b/d/f/r/s/l/a/c/n") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--analyze", type=Path, help="Analyze a saved log without serial access")
    parser.add_argument("--port", help="Explicit port of the identified I3 device")
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--step", type=parse_step, action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.analyze:
        result = summarize(args.analyze.read_text(encoding="utf-8", errors="replace"))
        args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
        return 0
    if not args.port or not 1 <= args.seconds <= 3600:
        parser.error("Capture needs --port and --seconds between 1 and 3600")
    steps = sorted(args.step, key=lambda step: step[0])
    if any(when >= args.seconds for when, _ in steps):
        parser.error("Every step must fall within the capture duration")
    import serial  # Supplied by PlatformIO's Python; analysis needs stdlib only.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = True
    port.rts = False
    port.port = args.port
    sent = []
    started = time.monotonic()
    try:
        port.open()
        with args.output.open("wb") as log:
            while time.monotonic() - started < args.seconds:
                elapsed = time.monotonic() - started
                while steps and steps[0][0] <= elapsed:
                    planned, command = steps.pop(0)
                    port.write(command.encode("ascii"))
                    sent.append({"planned_s": planned, "sent_s": elapsed, "command": command})
                data = port.read(4096)
                if data:
                    log.write(data)
                    log.flush()
    finally:
        port.close()
    result = summarize(args.output.read_text(encoding="utf-8", errors="replace"))
    result.update(port=args.port, duration_s=time.monotonic() - started, commands=sent)
    args.output.with_suffix(".json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
