#!/usr/bin/env python3
"""Generuje src/muzyka_dane.h: melodie (głos sopranowy) z korpusu music21, wyrównane do jednej oktawy.
Wszystkie utwory są w domenie publicznej (XVII-XIX w.)."""
import glob, os, sys, warnings
warnings.filterwarnings("ignore")
from music21 import corpus, converter, expressions, note, chord

KORPUS = os.path.dirname(corpus.__file__)

def abc_utwor(plik, tytul_abc):
    """wyciąga jeden utwór z wieloutworowego pliku ABC po tytule"""
    tekst = open(plik, errors="ignore").read()
    for blok in tekst.split("\nX:")[1:]:
        if tytul_abc in blok.split("\n", 3)[1] or ("T:" + tytul_abc) in blok:
            return converter.parse("X:" + blok, format="abc")
    raise SystemExit("brak: " + tytul_abc)

UTWORY = [
    # (tytuł, część Mszy, opis, źródło, tempo ćwierćnuty)
    ("Lobe den Herren", "Na wejście / uwielbienie", "Chwalcie Pana - Stralsund 1665, chorał J. S. Bacha",
     ("bach", "bwv137.5"), 100),
    ("Wachet auf, ruft uns die Stimme", "Adwent", "chorał adwentowy - J. S. Bach, BWV 140",
     ("bach", "bwv140.7"), 84),
    ("Cicha noc", "Boże Narodzenie", "Stille Nacht - F. X. Gruber, 1818",
     ("abc", "boehme20.abc", "STILLE NACHT HEILIGE NACHT"), 132),
    ("Brich an, o schönes Morgenlicht", "Boże Narodzenie", "Oratorium na Boże Narodzenie - J. S. Bach, BWV 248",
     ("bach", "bwv248.12-2"), 72),
    ("O Haupt voll Blut und Wunden", "Wielki Post", "chorał pasyjny - J. S. Bach, Pasja wg św. Mateusza",
     ("bach", "bwv244.54"), 66),
    ("Schmücke dich, o liebe Seele", "Na Komunię", "chorał eucharystyczny - J. S. Bach, BWV 180",
     ("bach", "bwv180.7"), 72),
    ("Jesu, meine Freude", "Na Komunię", "Jezu, radości moja - J. S. Bach, BWV 227",
     ("bach", "bwv227.1"), 76),
    ("Nun danket alle Gott", "Dziękczynienie", "Teraz dziękujmy Bogu - J. Crüger / J. S. Bach",
     ("bach", "bwv386"), 80),
]

def melodia(zrodlo):
    if zrodlo[0] == "abc":
        plik = glob.glob(os.path.join(KORPUS, "**", zrodlo[1]), recursive=True)[0]
        s = abc_utwor(plik, zrodlo[2])
        czesc = s.parts[0] if s.parts else s
    else:
        s = corpus.parse("bach/" + zrodlo[1])
        czesc = [p for p in s.parts if (p.partName or "").lower().startswith("sopran")][0]
    try:
        czesc = czesc.expandRepeats()
    except Exception:
        pass
    czesc = czesc.stripTies()
    wynik = []
    for el in czesc.flatten().notesAndRests:
        ql = float(el.quarterLength)
        if ql <= 0:
            continue
        ferm = any(isinstance(e, expressions.Fermata) for e in el.expressions)
        if isinstance(el, note.Rest):
            wynik.append((0, ql, False))
        elif isinstance(el, chord.Chord):
            wynik.append((max(p.midi for p in el.pitches), ql, ferm))
        else:
            wynik.append((el.pitch.midi, ql, ferm))
    return wynik

out = ["/* Wygenerowane przez tools/gen_muzyka.py (music21) - nie edytować */",
       "/* Melodie w domenie publicznej (głos sopranowy). */", ""]
spis = []
for i, (tytul, czesc, opis, zrodlo, tempo) in enumerate(UTWORY):
    m = melodia(zrodlo)
    ms_cw = 60000.0 / tempo
    najnizsza = min(x[0] for x in m if x[0])
    przes = 0
    while najnizsza + przes < 69: przes += 12      # głośniczek najlepiej brzmi w okolicy 450-1600 Hz
    while najnizsza + przes > 80: przes -= 12
    nuty, czasy = [], []
    for midi, ql, ferm in m:
        d = ql * ms_cw * (1.9 if ferm else 1.0)
        nuty.append(min(127, midi + przes) if midi else 0)
        czasy.append(int(round(d)))
    while nuty and nuty[-1] == 0:
        nuty.pop(); czasy.pop()
    while nuty and nuty[0] == 0:
        nuty.pop(0); czasy.pop(0)
    sek = sum(czasy) / 1000
    print(f"{i+1}. {tytul:35s} nut: {len(nuty):3d}  czas: {sek:5.1f} s  zakres: {min(n for n in nuty if n)}-{max(nuty)}", file=sys.stderr)
    out.append(f"static const u8 m{i}_n[] = {{{','.join(map(str, nuty))}}};")
    out.append(f"static const u16 m{i}_t[] = {{{','.join(map(str, czasy))}}};")
    spis.append(f'    {{ "{tytul}", "{czesc}", "{opis}", m{i}_n, m{i}_t, {len(nuty)} }},')

out.append("")
out.append("static const utwor_t utwory[] = {")
out += spis
out.append("};")
out.append("#define UTWORY_N ((int)(sizeof utwory / sizeof utwory[0]))")
# tabela częstotliwości (Hz) dla nut MIDI
cz = [int(round(440 * 2 ** ((n - 69) / 12))) for n in range(128)]
out.append("static const u16 czestotliwosc[128] = {" + ",".join(map(str, cz)) + "};")
open(sys.argv[1], "w", encoding="utf-8").write("\n".join(out) + "\n")
