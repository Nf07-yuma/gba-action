#!/usr/bin/env python3
"""GBA ROM ヘッダのチェックサム(0xBD)を計算して書き込む。"""
import sys

path = sys.argv[1]
with open(path, "r+b") as f:
    rom = bytearray(f.read())
    chk = 0
    for b in rom[0xA0:0xBD]:
        chk = (chk - b) & 0xFF
    chk = (chk - 0x19) & 0xFF
    rom[0xBD] = chk
    # 実機の ROM は 4 バイト境界に合わせる
    while len(rom) % 4:
        rom.append(0)
    f.seek(0)
    f.write(rom)
print(f"{path}: header checksum=0x{chk:02X} size={len(rom)}")
