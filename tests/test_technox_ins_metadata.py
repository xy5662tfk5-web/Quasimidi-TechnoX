import json
import unittest
from pathlib import Path

ROOT = Path(__file__).parents[1]

class TechnoxMetadataTests(unittest.TestCase):
    def test_all_single_sound_names_are_present(self):
        metadata = json.loads((ROOT / "data" / "technox_ins.json").read_text(encoding="utf-8"))
        banks = metadata["program_banks"]
        self.assertEqual(set(banks), {"A", "B", "C", "D"})
        self.assertEqual([len(banks[key]) for key in "ABCD"], [128, 128, 128, 128])
        self.assertEqual(sum(len(value) for value in banks.values()), 512)
        self.assertEqual(len(metadata["channel_10_drum_kits"]), 32)
        self.assertEqual(len(metadata["drum_maps"]), 24)
        self.assertEqual(len(metadata["controllers"]), 26)
        self.assertEqual(banks["A"][0]["name"], "303 Lead")
        self.assertEqual(banks["A"][0]["program"], 0)

    def test_manual_controller_paths_are_structured(self):
        metadata = json.loads((ROOT / "data" / "technox_ins.json").read_text(encoding="utf-8"))
        controls = metadata["manual_confirmed_midi_controls"]
        self.assertEqual(controls["cc0"]["values"]["0"], "A")
        self.assertEqual(controls["cc0"]["values"]["3"], "D")
        self.assertIn("nrpn", controls)
        self.assertIn("rpn", controls)

if __name__ == "__main__":
    unittest.main()
