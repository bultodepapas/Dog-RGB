from collections import deque
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class EventQueueModel:
    def __init__(self, capacity=16):
        self.capacity = capacity
        self.events = deque()
        self.dropped = 0
        self.high_water = 0

    def send(self, event):
        if len(self.events) >= self.capacity:
            self.dropped += 1
            return False
        self.events.append(event)
        self.high_water = max(self.high_water, len(self.events))
        return True

    def drain(self):
        drained = list(self.events)
        self.events.clear()
        return drained


class WifiEventQueueTests(unittest.TestCase):
    def test_fifo_order_preserves_final_connection_state(self):
        queue = EventQueueModel()
        for event in ("got_ip", "ap_client_connected", "disconnected"):
            self.assertTrue(queue.send(event))

        connected = False
        for event in queue.drain():
            if event == "got_ip":
                connected = True
            elif event == "disconnected":
                connected = False
        self.assertFalse(connected)

    def test_saturation_is_bounded_and_counted(self):
        queue = EventQueueModel(capacity=16)
        for index in range(16):
            self.assertTrue(queue.send(index))
        for index in range(16, 21):
            self.assertFalse(queue.send(index))

        self.assertEqual(queue.high_water, 16)
        self.assertEqual(queue.dropped, 5)
        self.assertEqual(queue.drain(), list(range(16)))

    def test_callback_only_enqueues_and_never_mutates_wifi_state(self):
        wifi_cpp = (ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        callback = wifi_cpp.split("void on_wifi_event", 1)[1].split(
            "void process_wifi_event", 1
        )[0]

        self.assertIn("xQueueSend", callback)
        self.assertIn("wifi_event_dropped_pending.fetch_add", callback)
        for forbidden in (
            "wifi_sta_connected =",
            "wifi_sta_connecting =",
            "wifi_diag.",
            "WiFi.",
            "Serial.",
        ):
            self.assertNotIn(forbidden, callback)

    def test_main_tick_drains_before_running_state_machine(self):
        wifi_cpp = (ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        tick = wifi_cpp.split("void tick(unsigned long now_ms)", 1)[1]

        self.assertLess(tick.index("drain_wifi_events(now_ms)"), tick.index("last_wifi_check_ms"))
        self.assertIn("xQueueCreateStatic", wifi_cpp)
        self.assertIn("WIFI_EVENT_QUEUE_LENGTH = 16", wifi_cpp)
        self.assertIn("last_wifi_check_ms = now_ms - WIFI_RETRY_INTERVAL_MS", wifi_cpp)
        self.assertIn("const bool reconcile_ap_state = drain_wifi_events(now_ms)", tick)
        self.assertIn("if (reconcile_ap_state)", tick)

    def test_station_count_uses_events_with_slow_fallback_reconciliation(self):
        config_h = (ROOT / "include/config.h").read_text(encoding="utf-8")
        wifi_cpp = (ROOT / "src/wifi/wifi_mgr.cpp").read_text(encoding="utf-8")
        self.assertIn("AP_CLIENT_POLL_MS = 60000", config_h)
        self.assertIn("ARDUINO_EVENT_WIFI_AP_STACONNECTED", wifi_cpp)
        self.assertIn("ARDUINO_EVENT_WIFI_AP_STADISCONNECTED", wifi_cpp)
        self.assertIn("update_ap_station_count();", wifi_cpp)
        self.assertEqual(
            wifi_cpp.count("wifi_diag.current_ap_channel = read_wifi_channel();"), 1
        )

    def test_queue_diagnostics_are_exposed(self):
        portal_cpp = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")
        self.assertIn('wifiDiag["event_queue_overflow_count"]', portal_cpp)
        self.assertIn('wifiDiag["event_queue_high_water"]', portal_cpp)


if __name__ == "__main__":
    unittest.main()
