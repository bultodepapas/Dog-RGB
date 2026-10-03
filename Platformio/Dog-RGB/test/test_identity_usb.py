from pathlib import Path
import json
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from identity_usb import transact


class Port:
    def __init__(self, status="saved", generation=8):
        self.lines = []; self.sent = []; self.status = status; self.generation = generation
    def reset_input_buffer(self): self.lines.clear()
    def write(self, data):
        self.sent.append(data)
        if data == b"\nj": self.lines.append(b"[IDENTITY] version=1 status=ready generation=7 configured=1\n")
        elif data.startswith(b"{"):
            self.lines.append(f"[IDENTITY] version=1 status={self.status} generation={self.generation} configured=1\n".encode())
        elif data == b"\n": self.lines.append(b"[IDENTITY] version=1 status=cancelled generation=7 configured=1\n")
    def readline(self): return self.lines.pop(0) if self.lines else b""


class IdentityUsbTests(unittest.TestCase):
    request = {"name": "FREYA", "phone": "+100000000000", "qr_kind": "whatsapp"}
    def test_handshake_generation_and_private_receipt(self):
        port = Port(); result = transact(port, self.request)
        self.assertEqual(json.loads(port.sent[1])["expected_generation"], 7)
        self.assertEqual(result, {"operation": "save", "status": "saved", "generation": 8, "configured": True})
        self.assertNotIn("phone", result); self.assertEqual(port.sent[-1], b"\n")
    def test_status_cancels_without_body(self):
        port = Port(); self.assertEqual(transact(port)["generation"], 7)
        self.assertFalse(any(data.startswith(b"{") for data in port.sent))
    def test_conflict_is_not_retried(self):
        port = Port("conflict", 9)
        with self.assertRaisesRegex(RuntimeError, "conflict"): transact(port, self.request)
        self.assertEqual(sum(data.startswith(b"{") for data in port.sent), 1)
        self.assertEqual(port.sent[-1], b"\n")
    def test_no_payload_without_supported_handshake(self):
        port = Port()
        with patch("identity_usb.read_reply", side_effect=RuntimeError("unsupported")):
            with self.assertRaises(RuntimeError): transact(port, self.request)
        self.assertEqual(port.sent, [b"\nj", b"\n"])
    def test_oversize_rejected_before_transmission(self):
        port = Port()
        with self.assertRaises(ValueError): transact(port, dict(self.request, name="X"*513))
        self.assertFalse(any(data.startswith(b"{") for data in port.sent))


if __name__ == "__main__": unittest.main()
