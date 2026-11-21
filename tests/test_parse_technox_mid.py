import importlib.util, struct, unittest
from pathlib import Path
MODULE = Path(__file__).with_name("parse_technox_mid.py")
spec = importlib.util.spec_from_file_location("parse_technox_mid", MODULE)
parser = importlib.util.module_from_spec(spec); spec.loader.exec_module(parser)
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
if __name__ == "__main__": unittest.main()
