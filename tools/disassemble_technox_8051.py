#!/usr/bin/env python3
"""Reachability-guided MCS-51 disassembler for the descrambled TechnoX IC4 ROM.

This is a static analysis aid, not a full emulator. It decodes reachable code
from the reset vector and interrupt vectors, follows direct branches/calls,
and marks indirect control flow and unresolved banking explicitly.
"""
from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
from pathlib import Path

from analyze_technox_rom import descramble_ic4, report


def rel_target(pc: int, length: int, rel: int) -> int:
    return (pc + length + (rel if rel < 0x80 else rel - 0x100)) & 0xFFFF


def instruction_length(op: int) -> int:
    # MCS-51 fixed-length encoding; AJMP/ACALL occupy every xx1/xx11 slot.
    if op & 0x1F == 0x01:  # AJMP
        return 2
    if op & 0x1F == 0x11:  # ACALL
        return 2
    if op in (0x02, 0x10, 0x12, 0x20, 0x30, 0x43, 0x53, 0x63,
              0x75, 0x85, 0x90, 0xB4, 0xB5, 0xD5):
        return 3
    if (op in (0x05, 0x15, 0x24, 0x25, 0x34, 0x35, 0x40, 0x42,
               0x44, 0x45, 0x50, 0x52, 0x54, 0x55, 0x60, 0x62,
               0x64, 0x65, 0x70, 0x72, 0x74, 0x80, 0x82, 0x86,
               0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E,
               0x8F, 0x92, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
               0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA2,
               0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD,
               0xAE, 0xAF, 0xB0, 0xB2, 0xC0, 0xC2, 0xC5, 0xD0,
               0xD2, 0xE5, 0xF5)):
        return 2
    if 0x76 <= op <= 0x7F or 0xD8 <= op <= 0xDF:
        return 2
    if 0xB6 <= op <= 0xBF:
        return 3
    if 0xE2 <= op <= 0xE3 or 0xF2 <= op <= 0xF3:
        return 1
    # The remaining 8051 opcodes are single-byte instructions.
    return 1


def decode(code: bytes, pc: int) -> tuple[int, str, list[int], str]:
    op = code[pc]
    n = instruction_length(op)
    b1 = code[(pc + 1) & 0xFFFF] if n > 1 else 0
    b2 = code[(pc + 2) & 0xFFFF] if n > 2 else 0
    raw = [op] + ([b1] if n > 1 else []) + ([b2] if n > 2 else [])
    d = lambda x: f"0x{x:02X}"
    direct = lambda x: f"0x{x:02X}"
    rel = lambda x: f"0x{rel_target(pc, n, x):04X}"
    if op == 0x02: m = f"LJMP 0x{(b1<<8)|b2:04X}"
    elif op == 0x12: m = f"LCALL 0x{(b1<<8)|b2:04X}"
    elif op & 0x1F == 0x01:
        target = ((pc + 2) & 0xF800) | ((op & 0xE0) << 3) | b1
        m = f"AJMP 0x{target:04X}"
    elif op & 0x1F == 0x11:
        target = ((pc + 2) & 0xF800) | ((op & 0xE0) << 3) | b1
        m = f"ACALL 0x{target:04X}"
    elif op == 0x10: m = f"JBC {direct(b1)}, {rel(b2)}"
    elif op == 0x20: m = f"JB {direct(b1)}, {rel(b2)}"
    elif op == 0x30: m = f"JNB {direct(b1)}, {rel(b2)}"
    elif op in (0x40,0x50,0x60,0x70,0x80):
        m = f"{ {0x40:'JC',0x50:'JNC',0x60:'JZ',0x70:'JNZ',0x80:'SJMP'}[op] } {rel(b1)}"
    elif op == 0x90: m = f"MOV DPTR, #0x{(b1<<8)|b2:04X}"
    elif op == 0xE0: m = "MOVX A, @DPTR"
    elif op == 0xF0: m = "MOVX @DPTR, A"
    elif op in (0xE2,0xE3): m = f"MOVX A, @R{op&1}"
    elif op in (0xF2,0xF3): m = f"MOVX @R{op&1}, A"
    elif op == 0x73: m = "JMP @A+DPTR"
    elif op == 0x22: m = "RET"
    elif op == 0x32: m = "RETI"
    elif op == 0x85: m = f"MOV {direct(b2)}, {direct(b1)}"
    elif op == 0x75: m = f"MOV {direct(b1)}, #0x{b2:02X}"
    elif op in (0x43,0x53,0x63):
        m = f"{ {0x43:'ORL',0x53:'ANL',0x63:'XRL'}[op] } {direct(b1)}, #0x{b2:02X}"
    elif op == 0xD5: m = f"DJNZ {direct(b1)}, {rel(b2)}"
    elif 0xD8 <= op <= 0xDF: m = f"DJNZ R{op&7}, {rel(b1)}"
    elif 0xB4 <= op <= 0xBF:
        if op == 0xB4: m = f"CJNE A, #0x{b1:02X}, {rel(b2)}"
        elif op == 0xB5: m = f"CJNE A, {direct(b1)}, {rel(b2)}"
        elif op <= 0xB7: m = f"CJNE @R{op&1}, #0x{b1:02X}, {rel(b2)}"
        else: m = f"CJNE R{op&7}, #0x{b1:02X}, {rel(b2)}"
    else:
        m = f"DB 0x{op:02X}"
        # Mark common 1/2-byte opcodes with recognizable names in the report.
        names = {0x00:"NOP",0x03:"RR A",0x04:"INC A",0x13:"RRC A",
                 0x14:"DEC A",0x22:"RET",0x23:"RL A",0x32:"RETI",
                 0x33:"RLC A",0x73:"JMP @A+DPTR",0x83:"MOVC A,@A+PC",
                 0x84:"DIV AB",0x93:"MOVC A,@A+DPTR",0xA3:"INC DPTR",
                 0xA4:"MUL AB",0xC3:"CLR C",0xD3:"SETB C",
                 0xD4:"DA A",0xE4:"CLR A",0xF4:"CPL A"}
        if op in names: m = names[op]
        elif op in (0x05,0x15,0x24,0x25,0x34,0x35,0x44,0x45,0x54,0x55,0x64,0x65,0x74,0x94,0x95,0xE5,0xF5):
            m = f"OP_{op:02X} 0x{b1:02X}"
        elif 0x08 <= op <= 0x0F: m = f"INC R{op&7}"
        elif 0x18 <= op <= 0x1F: m = f"DEC R{op&7}"
        elif 0x28 <= op <= 0x2F: m = f"ADD A, R{op&7}"
        elif 0x38 <= op <= 0x3F: m = f"ADDC A, R{op&7}"
        elif 0x48 <= op <= 0x4F: m = f"ORL A, R{op&7}"
        elif 0x58 <= op <= 0x5F: m = f"ANL A, R{op&7}"
        elif 0x68 <= op <= 0x6F: m = f"XRL A, R{op&7}"
        elif 0x78 <= op <= 0x7F: m = f"MOV R{op&7}, #0x{b1:02X}"
        elif 0xE8 <= op <= 0xEF: m = f"MOV A, R{op&7}"
        elif 0xF8 <= op <= 0xFF: m = f"MOV R{op&7}, A"
        elif op in (0xA5,): m = "RESERVED 0xA5"
    return n, m, raw, ""


@dataclass
class Insn:
    pc: int
    size: int
    text: str
    raw: str


def disassemble(code: bytes, entries: list[int], limit: int) -> tuple[dict[int, Insn], set[int], set[int]]:
    pending = deque(entries)
    decoded: dict[int, Insn] = {}
    calls: set[int] = set()
    branches: set[int] = set()
    while pending and len(decoded) < limit:
        pc = pending.popleft() & 0xFFFF
        while pc < 0x10000 and len(decoded) < limit:
            if pc in decoded:
                break
            n, text, raw, _ = decode(code, pc)
            if pc + n > 0x10000:
                break
            decoded[pc] = Insn(pc, n, text, " ".join(f"{x:02X}" for x in raw))
            op = code[pc]
            fall = (pc + n) & 0xFFFF
            if op in (0x22, 0x32, 0x73):  # returns / computed jump
                break
            if op in (0x02, 0x12) or (op & 0x1F in (0x01, 0x11)):
                target = int(text.split("0x")[-1], 16)
                branches.add(target)
                if op in (0x12,) or (op & 0x1F) == 0x11:
                    calls.add(target)
                    pending.append(target)
                    pc = fall
                    continue
                pending.append(target)
                break
            if op in (0x80,):  # unconditional relative jump
                target = rel_target(pc, n, code[pc+1])
                branches.add(target); pending.append(target); break
            conditional = (op in (0x10,0x20,0x30,0x40,0x50,0x60,0x70,0xD5)
                           or 0xB4 <= op <= 0xBF or 0xD8 <= op <= 0xDF)
            if conditional:
                relbyte = code[pc+2] if op in (0x10,0x20,0x30,0xD5) or 0xB4 <= op <= 0xBF else code[pc+1]
                target = rel_target(pc, n, relbyte)
                branches.add(target); pending.append(target)
                pc = fall
                continue
            pc = fall
    return decoded, calls, branches


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--ic4", type=Path, default=Path("technox_IC4_mx27c1000_v105.bin"))
    ap.add_argument("--output", type=Path, default=Path("build/technox_ic4_disassembly.txt"))
    ap.add_argument("--entry", type=lambda x: int(x, 0), action="append")
    ap.add_argument("--max-instructions", type=int, default=50000)
    args = ap.parse_args()
    raw = args.ic4.read_bytes()
    report("ic4", raw)
    code = descramble_ic4(raw)[:0x10000]  # current MAME CPU map; upper half/banking unresolved
    entries = args.entry or [0xEB32, 0x000B, 0x0013, 0x001B]
    insns, calls, branches = disassemble(code, entries, args.max_instructions)
    lines = [
        "TechnoX IC4 MCS-51 reachable-code disassembly",
        "Caution: static reachability only; indirect jumps, tables, self-modifying state and ROM banking are unresolved.",
        f"Entries: {', '.join(f'0x{x:04X}' for x in entries)}",
        f"Decoded instructions: {len(insns)}",
        f"Direct call targets: {len(calls)}",
        f"Direct branch targets: {len(branches)}",
        "",
        "ADDRESS  BYTES       INSTRUCTION",
    ]
    for pc in sorted(insns):
        x = insns[pc]
        lines.append(f"{pc:04X}     {x.raw:<11} {x.text}")
    lines += ["", "DIRECT CALL TARGETS"]
    lines += [f"0x{x:04X}" for x in sorted(calls)]
    lines += ["", "DIRECT BRANCH TARGETS"]
    lines += [f"0x{x:04X}" for x in sorted(branches)]
    # Identify decoded MOVX sites and nearby statically visible DPTR immediates.
    lines += ["", "EXTERNAL DATA ACCESS INSTRUCTIONS (MOVX)"]
    for pc in sorted(insns):
        if code[pc] in (0xE0, 0xF0, 0xE2, 0xE3, 0xF2, 0xF3):
            lines.append(f"0x{pc:04X}: {insns[pc].text}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Disassembly: {len(insns)} instructions; {len(calls)} direct calls; {len(branches)} branch targets")
    print(f"MOVX sites: {sum(1 for pc in insns if code[pc] in (0xE0,0xF0,0xE2,0xE3,0xF2,0xF3))}")
    print(f"Listing written: {args.output}")


if __name__ == "__main__":
    main()
