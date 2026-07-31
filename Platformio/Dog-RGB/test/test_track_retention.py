from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TrackRingModel:
    """Host model of the firmware's partial-chunk rewrite policy."""

    def __init__(self, chunk_points=48, data_chunks=30):
        self.chunk_points = chunk_points
        self.max_chunks = data_chunks + 1
        self.max_points = chunk_points * data_chunks
        self.chunks = []
        self.active = []
        self.persisted_active_count = 0

    def add(self, point):
        self.active.append(point)
        due = len(self.active) % 3 == 0  # 15 s flush at 5 s sampling
        full = len(self.active) == self.chunk_points
        if due or full:
            self.flush(full)

    def flush(self, full):
        if len(self.active) <= self.persisted_active_count:
            return

        if self.persisted_active_count:
            self.chunks[-1] = self.active.copy()
        elif len(self.chunks) < self.max_chunks:
            self.chunks.append(self.active.copy())
        else:
            self.chunks.pop(0)
            self.chunks.append(self.active.copy())

        self.persisted_active_count = len(self.active)
        if full:
            self.active = []
            self.persisted_active_count = 0

    def visible(self):
        points = [point for chunk in self.chunks for point in chunk]
        points.extend(self.active[self.persisted_active_count :])
        return points[-self.max_points :]


class TrackRetentionTests(unittest.TestCase):
    def test_continuous_recording_keeps_latest_two_hours(self):
        ring = TrackRingModel()

        for point in range(2_400):
            ring.add(point)
            expected_start = max(0, point + 1 - ring.max_points)
            self.assertEqual(ring.visible(), list(range(expected_start, point + 1)))

    def test_firmware_uses_staging_chunk_and_partial_rewrite(self):
        gps_cpp = (ROOT / "src/gps/gps.cpp").read_text(encoding="utf-8")

        self.assertIn("TRACK_DATA_CHUNKS + 1", gps_cpp)
        self.assertIn("persisted_flush_count", gps_cpp)
        self.assertIn("const bool rewrite_active", gps_cpp)
        self.assertIn("if (rewrite_active)", gps_cpp)
        self.assertIn("if (full) {\n    track_current.flush_count = 0;", gps_cpp)

    def test_track_history_has_a_dedicated_nvs_partition(self):
        platformio_ini = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        partitions = (ROOT / "partitions_dog_rgb.csv").read_text(encoding="utf-8")
        storage_cpp = (ROOT / "src/storage/nvs_store.cpp").read_text(encoding="utf-8")

        self.assertIn("board_build.partitions = partitions_dog_rgb.csv", platformio_ini)
        self.assertIn("tracknvs, data, nvs", partitions)
        self.assertIn("0x30000", partitions)
        self.assertIn('legacy_track.begin("dogrgb_trk", false)', storage_cpp)
        self.assertIn('prefs_trk_instance.begin("dogrgb_trk", false, "tracknvs")', storage_cpp)


if __name__ == "__main__":
    unittest.main()
