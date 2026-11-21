import importlib.util, sys, unittest
from pathlib import Path

ROOT = Path(__file__).parents[1]
MID = ROOT / "tools" / "parse_technox_mid.py"
spec = importlib.util.spec_from_file_location("parse_technox_mid", MID)
parser = importlib.util.module_from_spec(spec); sys.modules[spec.name] = parser; spec.loader.exec_module(parser)
ANALYZER = ROOT / "tools" / "analyze_technox_parts.py"
spec2 = importlib.util.spec_from_file_location("analyze_technox_parts", ANALYZER)
module = importlib.util.module_from_spec(spec2); sys.modules[spec2.name] = module; spec2.loader.exec_module(module)

class PartAnalysisTests(unittest.TestCase):
    def test_byte_statistics_keep_offsets_opaque(self):
        frames = []
        for performance in range(5, 55):
            frames.append(bytes([0xF0,0x3F,0,0x22,0x44,5,performance])+b"Test    "+b"\xF7")
            frames.append(bytes([0xF0,0x3F,0,0x22,0x44,0,performance])+bytes(65)+b"\xF7")
            for part in range(1,5):
                payload = bytes([part] * 24)
                frames.append(bytes([0xF0,0x3F,0,0x22,0x44,part,performance])+payload+b"\xF7")
        # Build an SMF directly using the existing test helper.
        test_module_path = ROOT / "tests" / "test_parse_technox_mid.py"
        spec3 = importlib.util.spec_from_file_location("test_parse_technox_mid", test_module_path)
        test_module = importlib.util.module_from_spec(spec3); sys.modules[spec3.name] = test_module; spec3.loader.exec_module(test_module)
        result = module.analyze(test_module.smf(frames))
        self.assertEqual(result["part_frames"],200)
        self.assertEqual(result["parts"][0]["byte_statistics"][0]["constant_value"],1)
        self.assertEqual(result["parts"][3]["byte_statistics"][23]["constant_value"],4)

if __name__ == "__main__": unittest.main()
