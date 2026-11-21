import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from disassemble_technox_8051 import decode, disassemble, instruction_length, rel_target


class MCS51DisassemblerTests(unittest.TestCase):
    def test_instruction_lengths(self):
        self.assertEqual(instruction_length(0x02), 3)  # LJMP
        self.assertEqual(instruction_length(0x12), 3)  # LCALL
        self.assertEqual(instruction_length(0x11), 2)  # ACALL
        self.assertEqual(instruction_length(0x90), 3)  # MOV DPTR,#imm16
        self.assertEqual(instruction_length(0xE0), 1)  # MOVX A,@DPTR
        self.assertEqual(instruction_length(0x94), 2)  # SUBB A,#imm
        self.assertEqual(instruction_length(0x95), 2)  # SUBB A,direct
        for opcode in range(0x96, 0xA0):
            self.assertEqual(instruction_length(opcode), 1)  # SUBB A,@Ri / A,Rn
        self.assertEqual(instruction_length(0xB6), 3)  # CJNE @R0,#imm,rel


    def test_subb_register_decode(self):
        code = bytes([0x96, 0x9F])
        n0, text0, _, _ = decode(code, 0)
        n1, text1, _, _ = decode(code, 1)
        self.assertEqual((n0, text0), (1, "SUBB A, @R0"))
        self.assertEqual((n1, text1), (1, "SUBB A, R7"))

    def test_relative_branch_wrap_and_sign(self):
        self.assertEqual(rel_target(0x0100, 2, 0xFE), 0x0100)
        self.assertEqual(rel_target(0xFFFE, 2, 0x02), 0x0002)

    def test_ljmp_decode(self):
        code = bytes([0x02, 0xEB, 0x32])
        n, text, raw, _ = decode(code, 0)
        self.assertEqual(n, 3)
        self.assertEqual(text, "LJMP 0xEB32")
        self.assertEqual(raw, [0x02, 0xEB, 0x32])

    def test_reachable_calls_and_branches(self):
        code = bytearray([0x00] * 0x10000)
        code[0x0100:0x0105] = bytes([0x12, 0x02, 0x00, 0x80, 0xFE])
        code[0x0200] = 0x22
        insns, calls, branches = disassemble(bytes(code), [0x0100], 100)
        self.assertIn(0x0100, insns)
        self.assertIn(0x0200, insns)
        self.assertIn(0x0200, calls)
        self.assertIn(0x0103, branches)


if __name__ == "__main__":
    unittest.main()
