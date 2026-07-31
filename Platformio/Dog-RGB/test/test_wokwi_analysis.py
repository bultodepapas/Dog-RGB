import importlib.util
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "analyze_wokwi", PROJECT_ROOT / "tools/analyze_wokwi.py"
)
ANALYZER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ANALYZER)


def encode_uart_8n1(data: bytes, baud: int = 9600):
    bit_ns = round(1_000_000_000 / baud)
    transitions = [(0, 1)]
    now = bit_ns
    level = 1
    for byte in data:
        bits = [0] + [(byte >> bit) & 1 for bit in range(8)] + [1]
        for next_level in bits:
            if next_level != level:
                transitions.append((now, next_level))
                level = next_level
            now += bit_ns
    return transitions


class WokwiAnalysisTests(unittest.TestCase):
    def test_uart_decoder_and_nmea_checksum_validation(self):
        sentence = b"$GPRMC,broken*78\r\n"
        decoded = ANALYZER.decode_uart_8n1(encode_uart_8n1(sentence), 9600)
        self.assertEqual(decoded, sentence)
        self.assertEqual(ANALYZER.nmea_checksum_counts(decoded), (1, 0))
        self.assertEqual(
            ANALYZER.nmea_checksum_counts(decoded.replace(b"*78", b"*00")), (0, 1)
        )

    def test_serial_summary_rejects_overflow_and_accepts_expected_faults(self):
        log = "\n".join(
            [
                "Dog-RGB ESP32-S3 GPS-first base firmware",
                "[GPS_LINK] bytes_delta=900 nmea_delta=10 rmc_delta=5 gga_delta=5 "
                "checksum_fail_delta=2 parse_fail_delta=1 speed_spike_delta=1 "
                "stale_delta=0 overflow=0",
                "[GPS_FIX] raw=1 trusted=1 current=1 reason=ok",
                "[MOTION] mode=speed range=7",
                "[LED] mode=speed body_on=1 render=range day_mode=disabled",
                "[SIM_CTRL] ok command=mode value=speed",
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.log"
            path.write_text(log, encoding="utf-8")
            report = ANALYZER.analyze_serial(path)
        self.assertTrue(report["pass"])
        self.assertEqual(report["gps_totals"]["checksum_fail_delta"], 2)
        self.assertEqual(report["renders"], ["range"])

        overflow = log.replace("overflow=0", "overflow=1")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "overflow.log"
            path.write_text(overflow, encoding="utf-8")
            report = ANALYZER.analyze_serial(path)
        self.assertFalse(report["pass"])

    def test_vcd_parser_preserves_analyzer_scope_names(self):
        vcd = """$timescale 1 ns $end
$scope module logic $end
$var wire 1 ! D0 $end
$upscope $end
$scope module logic_gnss $end
$var wire 1 # D0 $end
$upscope $end
$enddefinitions $end
#0
0!
1#
#10
1!
0#
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scoped.vcd"
            path.write_text(vcd, encoding="utf-8")
            names, transitions, duration_ns = ANALYZER.parse_vcd(path)
        self.assertEqual(names, {"!": "logic.D0", "#": "logic_gnss.D0"})
        self.assertEqual(ANALYZER.signal_by_name(names, transitions, "logic.D0")[-1], (10, 1))
        self.assertEqual(duration_ns, 10)


if __name__ == "__main__":
    unittest.main()
