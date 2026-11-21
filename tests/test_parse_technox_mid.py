import importlib.util, struct, unittest, sys
from pathlib import Path
MODULE = Path(__file__).parents[1] / "tools" / "parse_technox_mid.py"
spec = importlib.util.spec_from_file_location("parse_technox_mid", MODULE)
parser = importlib.util.module_from_spec(spec); sys.modules[spec.name] = parser; spec.loader.exec_module(parser)
ROOT = MODULE.parents[1]
def vlq(n):
    out=[n & 127]; n >>= 7
    while n: out.insert(0,(n & 127)|128); n >>= 7
    return bytes(out)
def smf(frames):
    tr=bytearray()
    for f in frames: tr += b"\x00\xf0"+vlq(len(f)-1)+f[1:]
    tr += b"\x00\xff\x2f\x00"
    return b"MThd"+struct.pack(">IHHH",6,0,1,96)+b"MTrk"+struct.pack(">I",len(tr))+tr
class ParserTests(unittest.TestCase):
    def test_extract_exact_frame(self):
        f=b"\xf0\x3f\x00\x22\x44\x05\x05"+b"Test    "+b"\xf7"
        self.assertEqual(parser.extract_sysex(smf([f]))[0].raw,f)
    def test_incomplete_sysex_rejected(self):
        raw=b"MThd"+struct.pack(">IHHH",6,0,1,96)+b"MTrk"+struct.pack(">I",5)+b"\x00\xf0\x02\x3f\x00"
        with self.assertRaises(parser.MidiFormatError): parser.extract_sysex(raw)
    def test_repository_bank_is_50_lossless_records(self):
        source=ROOT/"TECHNOX.MID"
        if not source.exists(): self.skipTest("TECHNOX.MID fixture not present")
        frames=parser.extract_sysex(source.read_bytes())
        bank=parser.decode_bank(source.read_bytes())
        self.assertEqual(len(frames),300)
        self.assertEqual(len(bank),50)
        self.assertEqual([p.index for p in bank],list(range(5,55)))
        self.assertTrue(all(len(p.part_frames)==4 for p in bank))
        original=[f.raw for f in frames]
        rebuilt=[]
        for p in bank:
            rebuilt.extend([p.name_frame,p.common_frame,*p.part_frames])
        self.assertEqual(rebuilt,original)
if __name__ == "__main__": unittest.main()
