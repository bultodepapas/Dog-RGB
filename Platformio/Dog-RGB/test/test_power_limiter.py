from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PIXELS_PER_BUS = 24


def rgb_to_rgbw(rgb):
    red, green, blue = rgb
    white = min(red, green, blue)
    return red - white, green - white, blue - white, white


def apply_scale(value, scale):
    return value if scale == 255 else (value * scale) // 255


def apply_brightness(value, brightness):
    return value if brightness == 255 else (value * (brightness + 1)) >> 8


def estimate_ma(
    buses,
    *,
    brightness=255,
    scale=255,
    base_ma=200,
    rgb_channel_ma=20,
    white_channel_ma=20,
):
    weighted = 0
    for bus in buses:
        for rgb in bus:
            red, green, blue, white = rgb_to_rgbw(rgb)
            red = apply_brightness(apply_scale(red, scale), brightness)
            green = apply_brightness(apply_scale(green, scale), brightness)
            blue = apply_brightness(apply_scale(blue, scale), brightness)
            white = apply_brightness(apply_scale(white, scale), brightness)
            weighted += (red + green + blue) * rgb_channel_ma
            weighted += white * white_channel_ma
    return base_ma + (weighted + 254) // 255


def largest_safe_scale(buses, budget_ma, **kwargs):
    if estimate_ma(buses, scale=255, **kwargs) <= budget_ma:
        return 255
    low, high = 0, 255
    while low < high:
        mid = (low + high + 1) // 2
        if estimate_ma(buses, scale=mid, **kwargs) <= budget_ma:
            low = mid
        else:
            high = mid - 1
    return low


def solid(rgb):
    return [rgb] * PIXELS_PER_BUS


class PowerLimiterTests(unittest.TestCase):
    def test_rgb_to_rgbw_black_primaries_and_white(self):
        self.assertEqual(rgb_to_rgbw((0, 0, 0)), (0, 0, 0, 0))
        self.assertEqual(rgb_to_rgbw((255, 0, 0)), (255, 0, 0, 0))
        self.assertEqual(rgb_to_rgbw((0, 255, 0)), (0, 255, 0, 0))
        self.assertEqual(rgb_to_rgbw((0, 0, 255)), (0, 0, 255, 0))
        self.assertEqual(rgb_to_rgbw((255, 255, 255)), (0, 0, 0, 255))
        self.assertEqual(rgb_to_rgbw((90, 60, 30)), (60, 30, 0, 30))

    def test_black_consumes_only_configured_base_current(self):
        self.assertEqual(estimate_ma([solid((0, 0, 0)), solid((0, 0, 0))]), 200)

    def test_primaries_and_white_use_one_channel_per_pixel(self):
        expected_one_bus = 200 + PIXELS_PER_BUS * 20
        for color in ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)):
            with self.subTest(color=color):
                self.assertEqual(estimate_ma([solid(color)]), expected_one_bus)

    def test_brightness_model_matches_neopixel_integer_scaling(self):
        # Adafruit_NeoPixel stores brightness 77 as factor 78/256, making a
        # full channel value 77 after integer scaling.
        expected_led_ma = (2 * PIXELS_PER_BUS * 77 * 20 + 254) // 255
        self.assertEqual(
            estimate_ma([solid((255, 0, 0)), solid((255, 0, 0))], brightness=77),
            200 + expected_led_ma,
        )

    def test_global_limit_covers_both_buses_and_saturates_at_budget(self):
        buses = [solid((255, 255, 0)), solid((255, 255, 0))]
        requested = estimate_ma(buses)
        scale = largest_safe_scale(buses, 1000)
        limited = estimate_ma(buses, scale=scale)

        self.assertEqual(requested, 2120)
        self.assertLess(scale, 255)
        self.assertLessEqual(limited, 1000)
        self.assertGreater(estimate_ma(buses, scale=scale + 1), 1000)

    def test_same_scale_is_safe_for_asymmetric_bus_load(self):
        buses = [solid((255, 255, 0)), solid((0, 0, 255))]
        scale = largest_safe_scale(buses, 900)
        self.assertLessEqual(estimate_ma(buses, scale=scale), 900)

    def test_firmware_contract_keeps_render_transport_and_diagnostics_separate(self):
        led_frame_h = (ROOT / "include/led/led_frame.h").read_text(encoding="utf-8")
        led_bus_h = (ROOT / "include/led/led_bus.h").read_text(encoding="utf-8")
        led_bus_cpp = (ROOT / "src/led/led_bus.cpp").read_text(encoding="utf-8")
        limiter_cpp = (ROOT / "src/led/power_limiter.cpp").read_text(encoding="utf-8")
        led_ui_cpp = (ROOT / "src/led/led_ui.cpp").read_text(encoding="utf-8")
        portal = (ROOT / "src/web/portal_http.cpp").read_text(encoding="utf-8")

        self.assertIn("struct LedFrame", led_frame_h)
        self.assertIn("Rgb bus_a[LED_STRIP_COUNT]", led_frame_h)
        self.assertIn("Rgb bus_b[LED_STRIP_COUNT]", led_frame_h)
        self.assertIn("class LedBus", led_bus_h)
        self.assertIn("class PowerLimiter", (ROOT / "include/led/power_limiter.h").read_text(encoding="utf-8"))
        self.assertIn("Rgbw rgb_to_rgbw", led_bus_cpp)
        self.assertIn("limiter_.evaluate(frame", led_bus_cpp)
        self.assertIn("while (low < high)", limiter_cpp)
        self.assertIn("LIMIT_RELEASE_STEP", limiter_cpp)
        self.assertNotIn("Adafruit_NeoPixel", led_ui_cpp)
        self.assertIn("led_bus.show(led_frame)", led_ui_cpp)
        for field in (
            'power["requested_ma"]',
            'power["estimated_ma"]',
            'power["peak_requested_ma"]',
            'power["scale"]',
            'power["frames_limited"]',
        ):
            self.assertIn(field, portal)


if __name__ == "__main__":
    unittest.main()
