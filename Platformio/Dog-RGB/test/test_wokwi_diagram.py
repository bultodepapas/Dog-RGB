import copy
import importlib.util
import json
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "wokwi_diagram", PROJECT_ROOT / "tools/wokwi_diagram.py"
)
DIAGRAM_TOOL = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(DIAGRAM_TOOL)


class WokwiDiagramTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.canonical = json.loads(
            (PROJECT_ROOT / "diagram.json").read_text(encoding="utf-8")
        )

    def test_gnss_profile_removes_only_non_gnss_analyzer_inputs(self):
        result = DIAGRAM_TOOL.instrument(copy.deepcopy(self.canonical), "gnss")
        connections = {(item[0], item[1]) for item in result["connections"]}
        self.assertIn(("gnss:TX", "logic:D2"), connections)
        self.assertIn(("gnss:DEBUG", "logic:D3"), connections)
        self.assertIn(("xiao:GND", "logic:GND"), connections)
        self.assertNotIn(("xiao:D0", "logic:D0"), connections)
        self.assertNotIn(("xiao:D1", "logic:D1"), connections)
        self.assertNotIn(("xiao:D2", "logic:D4"), connections)
        self.assertNotIn(("xiao:D0", "strip_a:DIN"), connections)
        self.assertNotIn(("xiao:D1", "strip_b:DIN"), connections)
        logic = next(part for part in result["parts"] if part["id"] == "logic")
        self.assertEqual(logic["attrs"]["bufferSize"], "250000")

    def test_full_profile_preserves_all_connections(self):
        result = DIAGRAM_TOOL.instrument(copy.deepcopy(self.canonical), "full")
        self.assertEqual(result["connections"], self.canonical["connections"])
        logic = next(part for part in result["parts"] if part["id"] == "logic")
        self.assertEqual(logic["attrs"]["bufferSize"], "1000000")


if __name__ == "__main__":
    unittest.main()
