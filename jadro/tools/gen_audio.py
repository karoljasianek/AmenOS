#!/usr/bin/env python3
"""Generuje src/audio_dane.h: tablice fal dla syntezatora AmenOS (system nie ma FPU,
więc wszystko, co wymaga sinusów, liczymy tutaj).

 - SIN_T    - sinus,
 - ORG_T    - organy: rejestry 16' + 8' + 4' + 2 2/3' + 2' (jeden okres tonu 16', grany oktawę niżej),
 - FLET_T   - flet: łagodny ton z małą domieszką alikwotów,
 - NUTA_MHZ - częstotliwości nut MIDI w milihercach (strój równomiernie temperowany, a1 = 440 Hz).

Użycie: python3 tools/gen_audio.py src/audio_dane.h"""
import sys, math

N = 2048
WYJ = sys.argv[1] if len(sys.argv) > 1 else "src/audio_dane.h"

def tablica(harm):
    v = [sum(a * math.sin(2 * math.pi * h * i / N + f) for h, a, f in harm) for i in range(N)]
    m = max(abs(x) for x in v)
    return [int(round(x / m * 30000)) for x in v]

sinus = tablica([(1, 1.0, 0)])
# organy: harmoniczne względem tonu 16' (1 = 16', 2 = 8', 4 = 4', 6 = 2 2/3', 8 = 2', 12, 16 - mikstura)
organy = tablica([(1, 0.45, 0), (2, 1.0, 0), (3, 0.08, 0.3), (4, 0.62, 0.5), (6, 0.30, 1.1), (8, 0.36, 0.7),
                  (10, 0.06, 0.2), (12, 0.14, 1.9), (16, 0.10, 0.4)])
flet = tablica([(1, 1.0, 0), (2, 0.18, 0.4), (3, 0.06, 1.0)])

out = ["/* Wygenerowane przez tools/gen_audio.py - nie edytować ręcznie. */"]
for nazwa, t in (("SIN_T", sinus), ("ORG_T", organy), ("FLET_T", flet)):
    out.append(f"static const short {nazwa}[{N}] = {{")
    for i in range(0, N, 16):
        out.append("    " + ",".join(str(x) for x in t[i:i + 16]) + ",")
    out.append("};")
mhz = [int(round(440.0 * 2 ** ((n - 69) / 12) * 1000)) for n in range(128)]
out.append("static const unsigned int NUTA_MHZ[128] = {")
for i in range(0, 128, 8):
    out.append("    " + ",".join(str(x) for x in mhz[i:i + 8]) + ",")
out.append("};")
open(WYJ, "w").write("\n".join(out) + "\n")
print("zapisano", WYJ)
