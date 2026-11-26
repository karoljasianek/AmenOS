#!/usr/bin/env python3
"""Wyciaga polskie znaki z czcionki PSF (Lat2-VGA16) i zapisuje je jako tablice C."""
import gzip, struct, sys

PL = "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
SLOT0 = 0x95   # 0x95-0xA6: zostawia wolne ü (0x81), é (0x82), ä (0x84), ö (0x94)

def load_psf(path):
    data = gzip.open(path).read()
    if data[:2] == b"\x36\x04":            # PSF1
        mode, h = data[2], data[3]
        n = 512 if mode & 1 else 256
        glyphs = [data[4 + i*h: 4 + (i+1)*h] for i in range(n)]
        table, pos, idx = {}, 4 + n*h, 0
        if mode & 2:
            while idx < n and pos + 1 < len(data):
                u = struct.unpack_from("<H", data, pos)[0]; pos += 2
                if u == 0xFFFF: idx += 1
                elif u != 0xFFFE: table.setdefault(u, idx)
        return glyphs, table, h
    raise SystemExit("tylko PSF1")

glyphs, table, h = load_psf(sys.argv[1])
assert h == 16, h
out = ["/* Wygenerowane przez tools/gen_font.py z Lat2-VGA16.psf - nie edytowac */",
       "#define PL_SLOT0 0x%02X" % SLOT0,
       "#define PL_COUNT %d" % len(PL),
       "static const unsigned int pl_codepoints[PL_COUNT] = {" +
       ", ".join("0x%04X" % ord(c) for c in PL) + "};",
       "static const unsigned char pl_glyphs[PL_COUNT][16] = {"]
for c in PL:
    g = glyphs[table[ord(c)]]
    out.append("  {" + ",".join("0x%02X" % b for b in g) + "}, /* %s */" % c)
out.append("};")
open(sys.argv[2], "w", encoding="utf-8").write("\n".join(out) + "\n")
print("OK", len(PL), "znakow")
