#!/usr/bin/env python3
"""Summarize Dog-RGB Wokwi serial logs and logic-analyzer VCD captures."""

from __future__ import annotations

import argparse
import bisect
import collections
import json
import re
from pathlib import Path
from typing import Any


FIELD_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
TAG_RE = re.compile(r"^\[([^\]]+)\]\s*(.*)$")
FATAL_MARKERS = (
    "guru meditation",
    "stack smashing",
    "abort() was called",
    "backtrace:",
    "warning: last reset was a crash",
    "warning: last reset was a brownout",
)


def scalar(value: str) -> Any:
    try:
        return int(value)
    except ValueError:
        try:
            return float(value)
        except ValueError:
            return value


def fields(text: str) -> dict[str, Any]:
    # Serial writers can occasionally interleave two records. Keep the first
    # occurrence so a later record cannot overwrite this tag's own fields.
    result: dict[str, Any] = {}
    for match in FIELD_RE.finditer(text):
        result.setdefault(match.group(1), scalar(match.group(2)))
    return result


def analyze_serial(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8", errors="replace")
    tags: collections.Counter[str] = collections.Counter()
    fix_reasons: collections.Counter[str] = collections.Counter()
    motion_modes: set[str] = set()
    motion_usable: collections.Counter[str] = collections.Counter()
    segment_reasons: collections.Counter[str] = collections.Counter()
    led_modes: set[str] = set()
    renders: set[str] = set()
    day_states: set[str] = set()
    totals = collections.Counter()
    max_overflow = 0
    min_heap: int | None = None
    max_loop_us = 0
    sim_ok = 0
    sim_errors = 0

    for line in text.splitlines():
        match = TAG_RE.match(line.strip())
        if not match:
            continue
        tag, payload = match.groups()
        tags[tag] += 1
        values = fields(payload)
        if tag == "GPS_LINK":
            for key in (
                "bytes_delta",
                "nmea_delta",
                "rmc_delta",
                "gga_delta",
                "checksum_fail_delta",
                "parse_fail_delta",
                "speed_spike_delta",
                "stale_delta",
            ):
                totals[key] += int(values.get(key, 0))
            max_overflow = max(max_overflow, int(values.get("overflow", 0)))
        elif tag == "GPS_FIX" and "reason" in values:
            fix_reasons[str(values["reason"])] += 1
        elif tag == "MOTION" and "mode" in values:
            motion_modes.add(str(values["mode"]))
            if "usable" in values:
                motion_usable[str(values["usable"])] += 1
            if "seg_reason" in values:
                segment_reasons[str(values["seg_reason"])] += 1
        elif tag == "LED":
            if "mode" in values:
                led_modes.add(str(values["mode"]))
            if "render" in values:
                renders.add(str(values["render"]))
            if "day_mode" in values:
                day_states.add(str(values["day_mode"]))
        elif tag == "SYS":
            heap = int(values.get("min_heap", values.get("heap", 0)))
            min_heap = heap if min_heap is None else min(min_heap, heap)
            max_loop_us = max(max_loop_us, int(values.get("loop_max_us", 0)))
        elif tag == "SIM_CTRL":
            if payload.startswith("ok "):
                sim_ok += 1
            elif payload.startswith("error "):
                sim_errors += 1

    lower = text.lower()
    fatals = [marker for marker in FATAL_MARKERS if marker in lower]
    booted = "Dog-RGB ESP32-S3 GPS-first base firmware" in text
    return {
        "path": str(path),
        "bytes": len(text.encode("utf-8")),
        "lines": len(text.splitlines()),
        "booted": booted,
        "tags": dict(sorted(tags.items())),
        "gps_totals": dict(sorted(totals.items())),
        "fix_reasons": dict(sorted(fix_reasons.items())),
        "motion_modes": sorted(motion_modes),
        "motion_usable": dict(sorted(motion_usable.items())),
        "segment_reasons": dict(sorted(segment_reasons.items())),
        "led_modes": sorted(led_modes),
        "renders": sorted(renders),
        "day_states": sorted(day_states),
        "max_uart_overflow": max_overflow,
        "minimum_reported_heap": min_heap,
        "maximum_reported_loop_us": max_loop_us,
        "sim_control_ok": sim_ok,
        "sim_control_errors": sim_errors,
        "fatal_markers": fatals,
        "pass": booted and max_overflow == 0 and not fatals and sim_errors == 0,
    }


def parse_vcd(path: Path) -> tuple[dict[str, str], dict[str, list[tuple[int, int]]], int]:
    names: dict[str, str] = {}
    transitions: dict[str, list[tuple[int, int]]] = collections.defaultdict(list)
    current_time = 0
    scale_ns = 1
    scopes: list[str] = []
    unit_scale = {"s": 1_000_000_000, "ms": 1_000_000, "us": 1_000, "ns": 1, "ps": 0.001}

    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("$timescale"):
            match = re.search(r"(\d+)\s*(s|ms|us|ns|ps)", line)
            if match:
                scale_ns = max(1, int(int(match.group(1)) * unit_scale[match.group(2)]))
        elif line.startswith("$scope"):
            parts = line.split()
            if len(parts) >= 3:
                scopes.append(parts[2])
        elif line.startswith("$upscope"):
            if scopes:
                scopes.pop()
        elif line.startswith("$var"):
            parts = line.split()
            if len(parts) >= 5:
                signal_name = parts[4]
                names[parts[3]] = ".".join((*scopes, signal_name))
        elif line.startswith("#"):
            current_time = int(line[1:]) * scale_ns
        elif len(line) >= 2 and line[0] in "01" and line[1:] in names:
            transitions[line[1:]].append((current_time, int(line[0])))
    return names, dict(transitions), current_time


def value_at(signal: list[tuple[int, int]], times: list[int], timestamp_ns: float) -> int:
    index = bisect.bisect_right(times, timestamp_ns) - 1
    return signal[index][1] if index >= 0 else 1


def decode_uart_8n1(signal: list[tuple[int, int]], baud: int) -> bytes:
    if len(signal) < 2:
        return b""
    bit_ns = 1_000_000_000.0 / baud
    times = [item[0] for item in signal]
    output = bytearray()
    previous = signal[0][1]
    cursor_ns = -1.0
    for timestamp_ns, level in signal[1:]:
        falling = previous == 1 and level == 0
        previous = level
        if not falling or timestamp_ns < cursor_ns:
            continue
        if value_at(signal, times, timestamp_ns + 0.5 * bit_ns) != 0:
            continue
        value = 0
        for bit in range(8):
            value |= value_at(signal, times, timestamp_ns + (1.5 + bit) * bit_ns) << bit
        if value_at(signal, times, timestamp_ns + 9.5 * bit_ns) != 1:
            continue
        output.append(value)
        cursor_ns = timestamp_ns + 10.0 * bit_ns
    return bytes(output)


def nmea_checksum_counts(data: bytes) -> tuple[int, int]:
    valid = 0
    invalid = 0
    for line in data.split(b"\r\n"):
        if not line.startswith(b"$") or b"*" not in line:
            continue
        body, expected = line[1:].rsplit(b"*", 1)
        if len(expected) != 2:
            invalid += 1
            continue
        checksum = 0
        for byte in body:
            checksum ^= byte
        try:
            matches = checksum == int(expected, 16)
        except ValueError:
            matches = False
        valid += int(matches)
        invalid += int(not matches)
    return valid, invalid


def signal_by_name(
    names: dict[str, str], transitions: dict[str, list[tuple[int, int]]], name: str
) -> list[tuple[int, int]]:
    for identifier, signal_name in names.items():
        if signal_name == name:
            return transitions.get(identifier, [])
    return []


def preferred_signal(
    names: dict[str, str],
    transitions: dict[str, list[tuple[int, int]]],
    *candidates: str,
) -> list[tuple[int, int]]:
    for candidate in candidates:
        signal = signal_by_name(names, transitions, candidate)
        if signal:
            return signal
    return []


def edge_intervals(signal: list[tuple[int, int]]) -> list[int]:
    edge_times = [timestamp for timestamp, _ in signal[1:]]
    return [right - left for left, right in zip(edge_times, edge_times[1:])]


def ws2812_bursts(signal: list[tuple[int, int]]) -> int:
    edge_times = [timestamp for timestamp, _ in signal]
    if len(edge_times) < 2:
        return 0
    return 1 + sum(1 for left, right in zip(edge_times, edge_times[1:]) if right - left > 50_000)


def analyze_vcd(path: Path, capture_profile: str = "full") -> dict[str, Any]:
    names, transitions, duration_ns = parse_vcd(path)
    by_name = {
        name: len(transitions.get(identifier, [])) for identifier, name in names.items()
    }
    uart_signal = preferred_signal(
        names, transitions, "logic_gnss.D0", "logic.D2", "D2"
    )
    uart_data = decode_uart_8n1(uart_signal, 9600)
    valid_nmea, invalid_nmea = nmea_checksum_counts(uart_data)
    tick_signal = preferred_signal(
        names, transitions, "logic_gnss.D1", "logic.D3", "D3"
    )
    intervals = edge_intervals(tick_signal)
    frequencies = [1_000_000_000.0 / interval for interval in intervals if interval > 0]
    led_a = preferred_signal(names, transitions, "logic.D0", "D0")
    led_b = preferred_signal(names, transitions, "logic.D1", "D1")
    status = preferred_signal(names, transitions, "logic.D4", "D4")
    result = {
        "path": str(path),
        "capture_profile": capture_profile,
        "duration_ms": round(duration_ns / 1_000_000.0, 3),
        "transitions": by_name,
        "uart_9600": {
            "decoded_bytes": len(uart_data),
            "valid_nmea": valid_nmea,
            "invalid_nmea": invalid_nmea,
        },
        "gnss_tick": {
            "ticks": max(0, len(tick_signal) - 1),
            "minimum_hz": round(min(frequencies), 3) if frequencies else None,
            "maximum_hz": round(max(frequencies), 3) if frequencies else None,
            "average_hz": round(sum(frequencies) / len(frequencies), 3) if frequencies else None,
        },
        "ws2812_bursts": {"strip_a": ws2812_bursts(led_a), "strip_b": ws2812_bursts(led_b)},
        "status_led_transitions": len(status),
    }
    gnss_ok = len(uart_signal) > 1 and len(tick_signal) > 1 and len(uart_data) > 0
    full_ok = len(led_a) > 1 and len(led_b) > 1 and len(status) > 1
    result["pass"] = gnss_ok and (capture_profile != "full" or full_ok)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", type=Path, required=True)
    parser.add_argument("--vcd", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--capture-profile", choices=("full", "gnss"), default="full")
    args = parser.parse_args()

    report: dict[str, Any] = {"serial": analyze_serial(args.serial)}
    if args.vcd and args.vcd.exists():
        report["vcd"] = analyze_vcd(args.vcd, args.capture_profile)
    report["pass"] = report["serial"]["pass"] and report.get("vcd", {"pass": True})["pass"]
    encoded = json.dumps(report, indent=2, sort_keys=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded + "\n", encoding="utf-8")
    print(encoded)
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
