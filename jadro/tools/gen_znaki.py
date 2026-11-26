#!/usr/bin/env python3
"""Generuje src/znaki_dane.h: tabele kodowań ISO-8859-2 i Windows-1250 (bajty 0x80-0xFF -> Unicode)
oraz zamienniki znaków łacińskich U+00A0-U+017F na czcionkę VGA (CP437 albo litera bez akcentu)."""
import sys, unicodedata

def tabela(kod):
    w = []
    for b in range(0x80, 0x100):
        try: w.append(ord(bytes([b]).decode(kod)))
        except UnicodeDecodeError: w.append(ord('?'))
    return w

ZAJETE = set(range(0x95, 0xA7))       # tu siedzą polskie litery AmenOS
cp437 = {}
for b in range(0x80, 0x100):
    if b in ZAJETE: continue
    cp437.setdefault(ord(bytes([b]).decode('cp437')), b)

zam = []
for c in range(0xA0, 0x180):
    ch = chr(c)
    if c in cp437: zam.append(cp437[c]); continue
    baza = unicodedata.normalize('NFD', ch)[0]
    if ord(baza) < 0x80 and baza.isprintable(): zam.append(ord(baza))
    elif ch == ' ': zam.append(32)
    else: zam.append(ord('?'))

out = ["/* Wygenerowane przez tools/gen_znaki.py - nie edytować */",
       "__attribute__((unused)) static const u16 kod_iso88592[128] = {" + ",".join(map(str, tabela('iso8859_2'))) + "};",
       "__attribute__((unused)) static const u16 kod_cp1250[128] = {" + ",".join(map(str, tabela('cp1250'))) + "};",
       "static const u8 lacinskie_vga[0xE0] = {" + ",".join(map(str, zam)) + "};  /* od U+00A0 */"]
open(sys.argv[1], 'w', encoding='utf-8').write("\n".join(out) + "\n")
print("OK")
