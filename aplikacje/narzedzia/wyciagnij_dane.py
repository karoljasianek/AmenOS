#!/usr/bin/env python3
"""Przenosi dane parafialne z jądra AmenOS (jadro/src, C) do aplikacji webowych:
modlitwy, tematy spowiedzi (nauki i pokuty), akt żalu, formuła rozgrzeszenia, liczby biblijne
kalkulatora, domyślne pieśni / Msze / ogłoszenia i melodie muzyki mszalnej.

Użycie: python3 aplikacje/narzedzia/wyciagnij_dane.py  (z głównego folderu repozytorium)
Wynik:  aplikacje/wspolne/dane.js"""
import re, json, ast, os

KORZEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
SRC = os.path.join(KORZEN, "jadro", "src")

def czytaj(nazwa):
    s = open(os.path.join(SRC, nazwa), encoding="utf-8").read()
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)

def blok(src, poczatek):
    i = src.index(poczatek)
    i = src.index("{", src.index("=", i))
    g, j = 0, i
    while True:
        c = src[j]
        if c == '"':                                   # pomiń napisy
            j += 1
            while src[j] != '"':
                j += 2 if src[j] == "\\" else 1
        elif c == "{": g += 1
        elif c == "}":
            g -= 1
            if g == 0: return src[i:j + 1]
        j += 1

def na_pythona(c, makra=None):
    for k, v in (makra or {}).items(): c = re.sub(r"\b%s\b" % k, v, c)
    c = c.replace("{", "[").replace("}", "]")
    c = re.sub(r"(?<![\w\"])0(?=\s*\])", "None", c)   # 0 kończące listę napisów
    return ast.literal_eval(c)

def napis(src, nazwa):
    i = src.index(nazwa)
    k = src.index(";", i)
    return "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', src[i:k]))

p = czytaj("powloka.c")
dane = {}
dane["modlitwy"] = [{"tytul": t, "linie": [x for x in l if x is not None]} for t, l in na_pythona(blok(p, "const modlitwa_t modlitwy[]"))]
dane["tematySpowiedzi"] = [{"klucze": [x for x in k if x is not None], "nauka": n, "pokuta": po}
                           for k, n, po in na_pythona(blok(p, "const temat_t sp_tematy[11]"))]
dane["aktZalu"] = napis(p, "const char *AKT_ZALU")
dane["rozgrzeszenie"] = napis(p, "const char *ROZGRZESZENIE").replace(" ┼", " ✝")
dane["piesniEtykiety"] = na_pythona(blok(p, "const char *piesni_etyk[PIESNI_N]"))
dane["msze"] = [{"etykieta": e, "godziny": g} for e, g in na_pythona(blok(p, "msza_t msze[MSZE_N]"))]
dane["ogloszenia"] = na_pythona(blok(p, "char ogloszenia[OGL_N][128]"))

k = czytaj("kalkulator.c")
dane["liczbyBiblijne"] = [{"n": n, "tytul": t, "opis": o} for n, t, o in na_pythona(blok(k, "BIBLIA[] ="))]

m = czytaj("muzyka_dane.h")
tab = {nm: [int(x) for x in v.split(",") if x.strip()] for nm, v in re.findall(r"static const u(?:8|16) (\w+)\[\] = \{([^}]*)\}", m)}
utw = []
for t, cz, op, n, c, ile in re.findall(r'\{ "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", (\w+), (\w+), (\d+) \}', m):
    utw.append({"tytul": t, "czesc": cz, "opis": op, "nuty": tab[n], "czasy": tab[c]})
dane["utwory"] = utw

wyj = os.path.join(KORZEN, "aplikacje", "wspolne", "dane.js")
with open(wyj, "w", encoding="utf-8") as f:
    f.write("/* Wygenerowane przez aplikacje/narzedzia/wyciagnij_dane.py z jądra AmenOS (jadro/src) - nie edytować ręcznie. */\n")
    f.write("const AMEN_DANE = " + json.dumps(dane, ensure_ascii=False, indent=1) + ";\n")
print("zapisano", wyj, {k: len(v) for k, v in dane.items() if isinstance(v, list)})
