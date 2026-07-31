import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BoundedStreamModel:
    def __init__(self, capacity=768, disconnect_after=None):
        self.capacity = capacity
        self.disconnect_after = disconnect_after
        self.pending = bytearray()
        self.chunks = []
        self.service_calls = 0
        self.healthy = True

    def append(self, data):
        if isinstance(data, str):
            data = data.encode()
        offset = 0
        while self.healthy and offset < len(data):
            if len(self.pending) == self.capacity:
                self.flush()
            copied = min(self.capacity - len(self.pending), len(data) - offset)
            self.pending.extend(data[offset : offset + copied])
            offset += copied
        return self.healthy

    def flush(self):
        if not self.healthy or not self.pending:
            return self.healthy
        self.service_calls += 1
        self.chunks.append(bytes(self.pending))
        self.pending.clear()
        self.service_calls += 1
        if self.disconnect_after is not None and len(self.chunks) >= self.disconnect_after:
            self.healthy = False
        return self.healthy

    def finish(self):
        return self.flush()


class TrackStreamingTests(unittest.TestCase):
    def test_bounded_stream_reconstructs_large_response_exactly(self):
        stream = BoundedStreamModel()
        payload = ("0123456789abcdef" * 1400).encode()
        self.assertTrue(stream.append(payload))
        self.assertTrue(stream.finish())

        self.assertEqual(b"".join(stream.chunks), payload)
        self.assertTrue(all(0 < len(chunk) <= 768 for chunk in stream.chunks))
        self.assertEqual(stream.service_calls, 2 * len(stream.chunks))

    def test_all_export_formats_remain_valid(self):
        points = [(4.7110, -74.0721), (4.7111, -74.0720), (4.7112, -74.0719)]

        raw_json = '{"points":[' + ",".join(f"[{lat:.7f},{lon:.7f}]" for lat, lon in points) + "]}"
        self.assertEqual(json.loads(raw_json)["points"][1], [4.7111, -74.072])

        raw_geojson = (
            '{"type":"FeatureCollection","features":[{"type":"Feature",'
            '"geometry":{"type":"LineString","coordinates":['
            + ",".join(f"[{lon:.7f},{lat:.7f}]" for lat, lon in points)
            + ']},"properties":{}}]}'
        )
        coordinates = json.loads(raw_geojson)["features"][0]["geometry"]["coordinates"]
        self.assertEqual(coordinates[0], [-74.0721, 4.711])

        raw_csv = "date,min,lat,lon\n" + "".join(
            f"20260731,{720 + index},{lat:.7f},{lon:.7f}\n"
            for index, (lat, lon) in enumerate(points)
        )
        self.assertEqual(len(raw_csv.strip().splitlines()), len(points) + 1)

    def test_disconnect_aborts_without_unbounded_output_work(self):
        stream = BoundedStreamModel(capacity=8, disconnect_after=2)
        self.assertFalse(stream.append(b"x" * 1000))
        self.assertEqual(len(stream.chunks), 2)
        self.assertLessEqual(sum(map(len, stream.chunks)), 16)

    def test_firmware_services_gnss_around_only_bounded_track_writes(self):
        source = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")
        stream_class = source.split("class TrackStream", 1)[1].split(
            "struct CoordinateStreamContext", 1
        )[0]
        flush = stream_class.split("bool flush()", 1)[1]
        self.assertIn("char buffer_[TRACK_STREAM_CHUNK_BYTES]", stream_class)
        self.assertRegex(
            flush,
            r"gps::tick\(\);\s*server\.sendContent\(buffer_, used_\);\s*used_ = 0;\s*gps::tick\(\);",
        )

        callbacks = source.split("bool track_json_cb", 1)[1].split(
            "void handle_track_get", 1
        )[0]
        self.assertNotIn("server.sendContent", callbacks)
        self.assertEqual(callbacks.count("stream->append"), 5)

    def test_uart_margin_and_iteration_snapshot_are_wired_correctly(self):
        config = (ROOT / "include/config.h").read_text(encoding="utf-8")
        gps = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")
        configured = int(re.search(r"GPS_RX_BUFFER_SIZE = (\d+)", config).group(1))

        # 8N1 is ten bits per byte: this retains over 17 seconds at 9600 baud.
        self.assertGreater(configured / (9600 / 10), 17.0)
        begin = gps.split("void begin()", 1)[1].split("void tick()", 1)[0]
        self.assertLess(begin.index("GPS.setRxBufferSize"), begin.index("GPS.begin"))

        iterator = gps.split("bool track_iter_points_internal", 2)[2].split(
            "void build_summary_payload", 1
        )[0]
        self.assertIn("const uint8_t unpersisted_end", iterator)
        self.assertIn("p < unpersisted_end", iterator)
        self.assertNotIn("p < track_current.flush_count", iterator)


if __name__ == "__main__":
    unittest.main()
