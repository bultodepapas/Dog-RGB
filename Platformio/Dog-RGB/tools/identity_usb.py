"""Configure identity on the explicitly selected stage-3 USB bench device.

No flashing, radio settings, messages or implicit retries. Public output omits
the contact. Uses the same validated NVS store as /api/identity.
"""
import argparse
import json
from pathlib import Path
import re
import time
import unicodedata

REPLY = re.compile(r"^\[IDENTITY\] version=1 status=(\w+) generation=(\d+) configured=([01])$")


def read_reply(port, wanted=None, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        match = REPLY.fullmatch(line)
        if match:
            result = {"status": match[1], "generation": int(match[2]), "configured": match[3] == "1"}
            if wanted is None or result["status"] == wanted:
                return result
    raise RuntimeError("No complete identity reply. Check stage-3 firmware and exclusive serial access; no retry performed.")


def transact(port, request=None):
    # Release any abandoned frame, then require a fresh protocol handshake before
    # sending data. On firmware without this protocol, only LF/j are transmitted.
    port.reset_input_buffer()
    port.write(b"\nj")
    try:
        ready = read_reply(port, "ready")
        if request is None:
            port.write(b"\n")
            reply = read_reply(port)
            if reply["status"] != "cancelled": raise RuntimeError("Identity status request was not cancelled cleanly")
            return {"operation": "status", **ready}
        request = dict(request, expected_generation=ready["generation"])
        payload = json.dumps(request, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(payload) > 512: raise ValueError("Identity request exceeds 512 bytes")
        port.write(payload + b"\n")
        reply = read_reply(port)
        if reply["status"] not in ("saved", "unchanged"):
            raise RuntimeError(f"Identity rejected: {reply['status']}; no retry performed")
        expected = (ready["generation"] + (reply["status"] == "saved")) & 0xffffffff
        if reply["generation"] != expected: raise RuntimeError("Unexpected identity generation in receipt")
        return {"operation": "save", **reply}
    finally:
        port.write(b"\n") # Also releases overflow/timeout drain state.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--status", action="store_true")
    mode.add_argument("--clear", action="store_true")
    mode.add_argument("--name")
    parser.add_argument("--phone")
    parser.add_argument("--channel", choices=("whatsapp", "call", "disabled"), default="whatsapp")
    parser.add_argument("--output", type=Path, help="Contact-free receipt JSON")
    args = parser.parse_args()
    if args.name is not None and not args.phone: parser.error("--name requires --phone with explicit international prefix")
    if args.name is None and args.phone: parser.error("--phone requires --name")
    request = None
    if args.clear: request = {"name": "", "phone": "", "qr_kind": "disabled"}
    elif args.name is not None:
        request = {"name": unicodedata.normalize("NFC", args.name), "phone": args.phone, "qr_kind": args.channel}
    import serial
    port = serial.Serial(port=None, baudrate=115200, timeout=0.15, write_timeout=2)
    port.dtr = True; port.rts = False; port.port = args.port
    try:
        port.open()
        result = transact(port, request)
    finally:
        port.close()
    encoded = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded + "\n", encoding="utf-8")
    print(encoded)


if __name__ == "__main__":
    main()
