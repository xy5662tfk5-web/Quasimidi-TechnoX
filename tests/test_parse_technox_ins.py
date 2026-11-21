import tempfile, unittest
from pathlib import Path
from parse_technox_ins import parse

class TechnoxInsTests(unittest.TestCase):
    def test_four_complete_program_banks_and_maps(self):
        source = ""
        for letter in "ABCD":
            source += f"[Bank {letter} Technox]\n" + "\n".join(f"{i}={letter}{i}" for i in range(128)) + "\n"
        source += "[Ch 10 Technox]\n" + "\n".join(f"{i}=Kit{i}" for i in range(32))
        source += "\n[Technox Controllers]\n0=Bank Select\n1=Modulation"
        source += "\n[Technox]\nBankSelMethod=1\nKey[4,0]=Standard\nKey[3,111]=Standard"
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"Technox.ins"; path.write_text(source,encoding="utf-8")
            obj=parse(path)
        self.assertEqual([len(x["programs"]) for x in obj["program_banks"]],[128]*4)
        self.assertEqual(len(obj["channel_10_drum_kits"]),32)
        self.assertEqual(obj["controllers"][0],{"cc":0,"name":"Bank Select"})
        self.assertEqual(len(obj["drum_map_program_assignments"]),2)

if __name__ == "__main__": unittest.main()
