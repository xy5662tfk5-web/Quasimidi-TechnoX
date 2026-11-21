import importlib.util, struct, unittest, sys
from pathlib import Path

MODULE = Path(__file__).with_name("parse_technox_mid.py")
spec = importlib.util.spec_from_file_location("parse_technox_mid", MODULE)
parser = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = parser
spec.loader.exec_module(parser)
ROOT = MODULE.parents[1]

def vlq(n):
    out = [n & 127]
    n >>= 7
    while n:
        out.insert(0, (n & 127) | 128)
        n >>= 7
    return bytes(out)

def smf(frames):
    tr = bytearray()
    for frame in frames:
        tr += b"\x00\xf0" + vlq(len(frame) - 1) + frame[1:]
    tr += b"\x00\xff\x2f\x00"
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96) + b"MTrk" + struct.pack(">I", len(tr)) + tr

class ParserTests(unittest.TestCase):
    def test_extract_exact_frame_and_header_fields(self):
        frame_bytes = b"\xf0\x3f\x00\x22\x44\x05\x05" + b"\x00Test    " + b"\xf7"
        frame = parser.extract_sysex(smf([frame_bytes]))[0]
        self.assertEqual(frame.raw, frame_bytes)
        self.assertEqual(frame.record_index, 5)
        self.assertEqual(frame.block_type, 5)
        self.assertEqual(len(frame.raw), 17)

    def test_decode_one_observed_performance_layout(self):
        frames = []
        frames.append(b"\xf0\x3f\x00\x22\x44\x05\x05" + b"\x00Fantasy" + b"\xf7")
        frames.append(b"\xf0\x3f\x00\x22\x44\x05\x00" + bytes(66) + b"\xf7")
        for part_type in (1, 2, 3, 4):
            frames.append(b"\xf0\x3f\x00\x22\x44\x05" + bytes([part_type]) + bytes(25) + b"\xf7")
        bank = parser.decode_bank(smf(frames), strict=False)
        self.assertEqual(len(bank), 1)
        self.assertEqual(bank[0].index, 5)
        self.assertEqual(bank[0].name, "Fantasy")
        self.assertEqual([len(p) for p in bank[0].part_frames], [33, 33, 33, 33])

    def test_incomplete_sysex_rejected(self):
        raw = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96) + b"MTrk" + struct.pack(">I", 5) + b"\x00\xf0\x02\x3f\x00"
        with self.assertRaises(parser.MidiFormatError):
            parser.extract_sysex(raw)

    def test_repository_bank_is_50_lossless_records(self):
        source = ROOT / "TECHNOX.MID"
        if not source.exists():
            self.skipTest("TECHNOX.MID fixture not present")
        frames = parser.extract_sysex(source.read_bytes())
        bank = parser.decode_bank(source.read_bytes())
        self.assertEqual(len(frames), 300)
        self.assertEqual(len(bank), 50)
        self.assertEqual([p.index for p in bank], list(range(5, 55)))
        self.assertTrue(all(len(p.part_frames) == 4 for p in bank))
        self.assertTrue(all(len(frame) == 33 for p in bank for frame in p.part_frames))
        original = [f.raw for f in frames]
        rebuilt = []
        for p in bank:
            rebuilt.extend([p.name_frame, p.common_frame, *p.part_frames])
        self.assertEqual(rebuilt, original)

if __name__ == "__main__":
    unittest.main()
