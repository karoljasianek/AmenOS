#!/usr/bin/env python3
"""Generuje src/grafika_dane.h:
 - czcionka 8x16 dla 256 znaków w układzie AmenOS (CP437, polskie litery w 0x95-0xA6),
 - ikony pulpitu 16x16 (pixel art),
 - tabela sinusów dla 60 pozycji wskazówek zegara."""
import gzip, struct, sys, math

def psf(path):
    d = gzip.open(path).read()
    if d[:2] != b"\x36\x04": raise ValueError("nie PSF1")
    mode, h = d[2], d[3]
    n = 512 if mode & 1 else 256
    gl = [d[4 + i*h: 4 + (i+1)*h] for i in range(n)]
    tab, pos, idx = {}, 4 + n*h, 0
    while idx < n and pos + 1 < len(d):
        u = struct.unpack_from("<H", d, pos)[0]; pos += 2
        if u == 0xFFFF: idx += 1
        elif u != 0xFFFE: tab.setdefault(u, idx)
    return gl, tab

gl, tab = psf(sys.argv[1])
# brakujące symbole: najpierw podobny znak z tej samej czcionki, potem inne czcionki 8x16
ZAMIENNIKI = {"►": "▶", "◄": "◀", "○": "◦"}
import glob, os
ZAPASOWE = []
for f in sorted(glob.glob(os.path.join(os.path.dirname(sys.argv[1]), "*16.psf.gz"))):
    try:
        g2, t2 = psf(f)
        if len(g2[0]) == 16: ZAPASOWE.append((g2, t2))
    except Exception:
        pass

def glif(ch):
    if ord(ch) in tab: return gl[tab[ord(ch)]]
    for g2, t2 in ZAPASOWE:
        if ord(ch) in t2: return g2[t2[ord(ch)]]
    z = ZAMIENNIKI.get(ch)
    if z and ord(z) in tab: return gl[tab[ord(z)]]
    return gl[tab[ord("?")]]
STEROWANIE = " ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
PL = "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
czcionka = []
for kod in range(256):
    if 0x95 <= kod <= 0xA6: ch = PL[kod - 0x95]
    elif kod < 32: ch = STEROWANIE[kod]
    elif kod == 0x7F: ch = "⌂"
    else: ch = bytes([kod]).decode("cp437")
    czcionka.append(glif(ch))
czcionka[0] = bytes(16)

PALETA = {'.': None, 'k': 0x101018, 'w': 0xFFFFFF, 'g': 0xB0B0B8, 'd': 0x505060, 'y': 0xF2C230, 'o': 0xE08020,
          'r': 0xD43030, 'b': 0x3478D8, 'l': 0x8CC8F4, 'n': 0x1C2C60, 'v': 0x8A3CC0, 'G': 0x3CB050, 'c': 0xF6F0DE,
          'B': 0x8A5A30, 'p': 0xF7A8C8}
IKONY = {
 "terminal": [
  "................", ".gggggggggggggg.", ".gkkkkkkkkkkkkg.", ".gkGkkkkkkkkkkg.", ".gkkGkkkkkkkkkg.",
  ".gkGkkGGGGkkkkg.", ".gkkkkkkkkkkkkg.", ".gkkkkkkkkkkkkg.", ".gkkkkkkkkkkkkg.", ".gkkkkkkkkkkkkg.",
  ".gggggggggggggg.", "......gddg......", "......gddg......", "....gggggggg....", "....dddddddd....", "................"],
 "kalendarz": [
  "................", "...kk......kk...", ".rrkkrrrrrrkkrr.", ".rrrrrrrrrrrrrr.", ".rrrrrrrrrrrrrr.",
  ".cccccccccccccc.", ".cddcddcddcddcc.", ".cccccccccccccc.", ".cddcddcGGcddcc.", ".cccccccGGccccc.",
  ".cddcddcddcddcc.", ".cccccccccccccc.", ".cddcddcddccccc.", ".cccccccccccccc.", ".gggggggggggggg.", "................"],
 "zegar": [
  "................", ".....kkkkkk.....", "...kkwwwwwwkk...", "..kwwwwkwwwwwk..", "..kwwwwkwwwwwk..",
  ".kwwwwwkwwwwwwk.", ".kwwwwwkwwwwwwk.", ".kwwwwwkkkkkwwk.", ".kwwwwwwwwwwwwk.", ".kwwwwwwwwwwwwk.",
  "..kwwwwwwwwwwk..", "..kwwwwwwwwwwk..", "...kkwwwwwwkk...", ".....kkkkkk.....", "................", "................"],
 "piesni": [
  "................", ".BBBBBBBBBBBBBB.", ".BkkkkkkkkkkkkB.", ".BkwkwwkwwkkkkB.", ".BkwkkwkkwkkkkB.",
  ".BkwkwwkwwkkkkB.", ".BkkkkkkkkkkkkB.", ".BkwwkwkwwkkkkB.", ".BkkwkwkkwkkkkB.", ".BkwwkwkwwkkkkB.",
  ".BkkkkkkkkkkkkB.", ".BBBBBBBBBBBBBB.", "......BB........", "......BB........", ".....BBBB.......", "................"],
 "muzyka": [
  "................", ".......yyyyyyy..", ".......yyyyyyy..", ".......y.....y..", ".......y.....y..",
  ".......y.....y..", ".......y.....y..", ".......y.....y..", ".......y.....y..", "....yyyy..yyyy..",
  "...yyyyy.yyyyy..", "...yyyyy.yyyyy..", "....yyy...yyy...", "................", "................", "................"],
 "internet": [
  "................", ".....bbbbbb.....", "...bbGGbbbbbb...", "..bGGGGbbbGGbb..", "..bGGGbbbGGGGb..",
  ".bbGGbbbbGGGbbb.", ".bbbbbbbbbGbbbb.", ".bbbbGGbbbbbbbb.", ".bbbGGGGbbbbbbb.", ".bbbGGGGGbbbGbb.",
  "..bbGGGGbbbGGb..", "..bbbGGbbbbGbb..", "...bbbbbbbbbb...", ".....bbbbbb.....", "................", "................"],
 "modlitwy": [
  "................", "................", "..cccccc.cccccc.", ".ccccccckcccccc.", ".cccrccckcccccc.",
  ".ccrrrcckcdddcc.", ".cccrccckcccccc.", ".cccrccckcdddcc.", ".ccccccckcccccc.", ".cddddcckcdddcc.",
  ".ccccccckcccccc.", ".cddddcckcdddcc.", ".ccccccckcccccc.", "..BBBBBBkBBBBBB.", "................", "................"],
 "spowiedz": [
  "................", "..wwwwwwwwwwww..", ".wwwwwwwwwwwwww.", ".wwwwwwvwwwwwww.", ".wwwwwwvwwwwwww.",
  ".wwwwvvvvvwwwww.", ".wwwwwwvwwwwwww.", ".wwwwwwvwwwwwww.", ".wwwwwwvwwwwwww.", ".wwwwwwwwwwwwww.",
  "..wwwwwwwwwwww..", "...www..........", "..ww............", ".w..............", "................", "................"],
 "komunia": [
  "................", ".......yy.......", "..y....yy....y..", "...y........y...", "......wwww......",
  ".....wwggww.....", "yy..wwwggwww..yy", "...wwggggggww...", "...wwggggggww...", "yy..wwwggwww..yy",
  ".....wwggww.....", "......wwww......", "...y........y...", "..y....yy....y..", ".......yy.......", "................"],
 "ogloszenia": [
  "................", "..cccccccccccc..", "..crrrrrrrrrrc..", "..cccccccccccc..", "..cddddddddddc..",
  "..cccccccccccc..", "..cdddddddddcc..", "..cccccccccccc..", "..cddddddddddc..", "..cccccccccccc..",
  "..cddddddccccc..", "..cccccccccccc..", "..cddddddddddc..", "..cccccccccccc..", "..gggggggggggg..", "................"],
 "msze": [
  ".......y........", "......yyy.......", ".......y........", "......www.......", ".....wwwww......",
  "....wwwwwww.....", "....wwwkwww.....", "....wwwkwww.....", "..wwwwwwwwwww...", "..wwwwwwwwwww...",
  "..wkwwwwwwwkw...", "..wkwwwkwwwkw...", "..wwwwwkwwwww...", "..wwwwwkwwwww...", "GGGGGGGGGGGGGGG.", "................"],
 "system": [
  "................", ".......yy.......", ".......yy.......", ".......yy.......", "....yyyyyyyy....",
  "....yyyyyyyy....", ".......yy.......", ".......yy.......", ".......yy.......", ".......yy.......",
  ".......yy.......", ".......yy.......", ".......yy.......", ".......yy.......", "................", "................"],
}
out = ["/* Wygenerowane przez tools/gen_grafika.py - nie edytować */",
       "static const u8 czcionka[256][16] = {"]
for g in czcionka: out.append("  {" + ",".join("0x%02X" % b for b in g) + "},")
out.append("};")
for nazwa, rys in IKONY.items():
    assert len(rys) == 16, (nazwa, len(rys))
    for i, w in enumerate(rys): assert len(w) == 16, (nazwa, i, w, len(w))
    piks = []
    for w in rys:
        for ch in w:
            c = PALETA[ch]
            piks.append("0xFF000000" if c is None else "0x%06X" % c)
    out.append(f"static const u32 ikona_{nazwa}[256] = {{" + ",".join(piks) + "};")
out.append("static const short sinus60[60] = {" + ",".join(str(round(1000*math.sin(2*math.pi*i/60))) for i in range(60)) + "};")
open(sys.argv[2], "w", encoding="utf-8").write("\n".join(out) + "\n")
print("OK:", len(IKONY), "ikon")
