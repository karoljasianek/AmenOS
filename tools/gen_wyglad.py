#!/usr/bin/env python3
"""Generuje src/wyglad_dane.h - grafikę nowoczesnego wyglądu pulpitu AmenOS:
 - czcionki wygładzane (Inter, licencja SIL OFL) w kilku rozmiarach, 8-bitowa alfa,
 - czcionka terminala 10x20 (DejaVu Sans Mono) dla 256 znaków w układzie AmenOS,
 - ikony programów 48, 32 i 24 piksele z kanałem alfa,
 - kursor myszy z wygładzaniem,
 - tapeta 480x270 (powiększana w systemie do rozdzielczości ekranu).

Użycie: python3 tools/gen_wyglad.py src/wyglad_dane.h
Potrzebne: Pillow, fontTools, czcionki Inter i DejaVu (pakiety fonts-inter, fonts-dejavu)."""
import sys, os, math, glob
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops
from fontTools.ttLib import TTFont

INTER = "/usr/share/fonts/opentype/inter/"
DEJAVU = "/usr/share/fonts/truetype/dejavu/"
WYJ = sys.argv[1] if len(sys.argv) > 1 else "src/wyglad_dane.h"

out = []
def tablica(typ, nazwa, dane, na_wiersz=24, fmt="0x%02X"):
    out.append(f"static const {typ} {nazwa}[{len(dane)}] = {{")
    for i in range(0, len(dane), na_wiersz):
        out.append("    " + ",".join(fmt % v for v in dane[i:i + na_wiersz]) + ",")
    out.append("};")

# ---------------------------------------------------------------- czcionki

PELNY = [chr(c) for c in range(0x20, 0x7F)] + [chr(c) for c in range(0xA0, 0x180)] + \
        list("–—‘’‚“”„…·•°×÷−√±†‰€←→↑↓►◄■♫♪✝⌂▲▼✓∞↩") + list("ΑΒΓΔΩαβγδπωЀАБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмнопрстуфхцчшщъыьэюя")
OGRANICZONY = [chr(c) for c in range(0x20, 0x7F)] + list("ąćęłńóśźżĄĆĘŁŃÓŚŹŻ–—·…×÷−√±„”°")

CZCIONKI = [   # nazwa w C, plik, rozmiar, zestaw znaków
    ("F_MALY",    INTER + "Inter-Regular.otf",         12, PELNY),
    ("F_TEKST",   INTER + "Inter-Regular.otf",         14, PELNY),
    ("F_TEKST_B", INTER + "Inter-SemiBold.otf",        14, PELNY),
    ("F_WWW",     INTER + "Inter-Regular.otf",         15, PELNY),
    ("F_WWW_B",   INTER + "Inter-SemiBold.otf",        15, PELNY),
    ("F_SREDNI",  INTER + "Inter-SemiBold.otf",        18, PELNY),
    ("F_H2",      INTER + "Inter-Bold.otf",            22, PELNY),
    ("F_DUZY",    INTER + "InterDisplay-SemiBold.otf", 28, PELNY),
    ("F_WIELKI",  INTER + "InterDisplay-Light.otf",    40, OGRANICZONY),
    ("F_OGROMNY", INTER + "InterDisplay-Light.otf",    72, OGRANICZONY),
]
ZAPAS = DEJAVU + "DejaVuSans.ttf"

def ma_znak(plik, ch, _c={}):
    if plik not in _c: _c[plik] = TTFont(plik).getBestCmap()
    return ord(ch) in _c[plik]

def renderuj(font, ch, asc):
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    adv = int(round(font.getlength(ch)))
    w, h = max(1, x1 - x0), max(1, y1 - y0)
    im = Image.new("L", (w + 4, h + 4), 0)
    ImageDraw.Draw(im).text((2 - x0, 2 - y0), ch, font=font, fill=255, anchor="ls")
    bb = im.getbbox()
    if not bb: return adv, 0, 0, 0, 0, b""
    im = im.crop(bb)
    xo = bb[0] - 2 + x0
    yo = bb[1] - 2 + y0 + asc          # od górnej krawędzi wiersza
    return adv, xo, yo, im.width, im.height, im.tobytes()

out.append("/* Wygenerowane przez tools/gen_wyglad.py - nie edytować ręcznie. */")
out.append("/* Czcionki: Inter (c) The Inter Project Authors, DejaVu - obie na wolnych licencjach (SIL OFL / Bitstream Vera). */")
out.append("typedef struct { u16 cp; s8 xo, yo; u8 w, h, adv; u32 off; } wglif_t;")
out.append("typedef struct { const wglif_t *g; int n; int wys, asc; const u8 *a; } wczcionka_t;")
opisy = []
for nazwa, plik, rozm, zestaw in CZCIONKI:
    font = ImageFont.truetype(plik, rozm)
    zapas = ImageFont.truetype(ZAPAS, rozm)
    asc, desc = font.getmetrics()
    wys = asc + desc
    glify, alfa = [], bytearray()
    for ch in sorted(set(zestaw), key=ord):
        f = font if ma_znak(plik, ch) else (zapas if ma_znak(ZAPAS, ch) else None)
        if f is None: continue
        adv, xo, yo, w, h, dane = renderuj(f, ch, asc)
        glify.append((ord(ch), xo, yo, w, h, adv, len(alfa)))
        alfa += dane
    m = nazwa.lower()
    tablica("u8", m + "_a", alfa, 32)
    out.append(f"static const wglif_t {m}_g[{len(glify)}] = {{")
    for g in glify: out.append("    { 0x%04X, %d, %d, %d, %d, %d, %d }," % g)
    out.append("};")
    opisy.append(f"    {{ {m}_g, {len(glify)}, {wys}, {asc}, {m}_a }},")
    print(f"{nazwa}: {len(glify)} znaków, {len(alfa)} B, wiersz {wys}px")
out.append("/* kolejność musi się zgadzać z enum F_... w kernel.h: " + ", ".join(c[0] for c in CZCIONKI) + " */")
out.append("static const wczcionka_t wczcionki[F_N] = {")
out += opisy
out.append("};")

# ---------------------------------------------------------------- czcionka terminala 10x20

TW, TH = 10, 20
STEROWANIE = " ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
PL = "ąćęłńóśźżĄĆĘŁŃÓŚŹŻ"
def znak_vga(kod):
    if 0x95 <= kod <= 0xA6: return PL[kod - 0x95]
    if kod < 32: return STEROWANIE[kod]
    if kod == 0x7F: return "⌂"
    return bytes([kod]).decode("cp437")

# kształty 8x16 z czcionki systemu (src/grafika_dane.h, ma już zamienniki brakujących znaków)
# - do znaków ramek i bloków, które muszą się stykać między komórkami
import re
_g = open(os.path.join(os.path.dirname(os.path.abspath(WYJ)), "grafika_dane.h")).read()
_c = re.search(r"czcionka\[256\]\[16\] = \{(.*?)\n\};", _g, re.S).group(1)
_b = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]{2}", _c)]
vga8x16 = [_b[i * 16:(i + 1) * 16] for i in range(256)]

mono = ImageFont.truetype(DEJAVU + "DejaVuSansMono.ttf", 16)
masc, mdesc = mono.getmetrics()
baza = 15
terminal = bytearray()
for kod in range(256):
    ch = znak_vga(kod)
    im = Image.new("L", (TW, TH), 0)
    if kod == 0 or ch == " ":
        pass
    elif 0xB0 <= kod <= 0xDF or kod in (0xFE,) or ch in "▲▼►◄↕↨↑↓→←↔∟▬":
        g = vga8x16[kod]              # powiększenie najbliższym sąsiadem: linie stykają się w każdej komórce
        if True:
            for y in range(TH):
                for x in range(TW):
                    if g[y * 16 // TH] & (0x80 >> (x * 8 // TW)): im.putpixel((x, y), 255)
    else:
        f = mono if ma_znak(DEJAVU + "DejaVuSansMono.ttf", ch) else ImageFont.truetype(ZAPAS, 15)
        ImageDraw.Draw(im).text((TW / 2, baza), ch, font=f, fill=255, anchor="ms")
    terminal += im.tobytes()
tablica("u8", "terminal_czc", terminal, 40)
out.append(f"#define TERM_ZW {TW}\n#define TERM_ZH {TH}")

# ---------------------------------------------------------------- ikony

S = 192   # rysujemy w powiększeniu, potem zmniejszamy (wygładzanie)

def hexc(h, a=255): return ((h >> 16) & 255, (h >> 8) & 255, h & 255, a)

def kafel(gora, dol):
    """zaokrąglony kwadrat z gradientem, delikatnym połyskiem i cieniem"""
    pad, r = 10, 44
    maska = Image.new("L", (S, S), 0)
    ImageDraw.Draw(maska).rounded_rectangle((pad, pad, S - pad, S - pad), r, fill=255)
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        c = tuple(int(gora[i] * (1 - t) + dol[i] * t) for i in range(3))
        gd.line((0, y, S, y), fill=c + (255,))
    cien = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cm = Image.new("L", (S, S), 0)
    ImageDraw.Draw(cm).rounded_rectangle((pad, pad + 6, S - pad, S - pad + 6), r, fill=90)
    cm = cm.filter(ImageFilter.GaussianBlur(6))
    cien.putalpha(cm)
    ik = Image.alpha_composite(Image.new("RGBA", (S, S), (0, 0, 0, 0)), cien)
    warstwa = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    warstwa.paste(grad, (0, 0), maska)
    # połysk u góry
    pol = Image.new("L", (S, S), 0)
    ImageDraw.Draw(pol).rounded_rectangle((pad + 2, pad + 2, S - pad - 2, S - pad - 2), r - 2, outline=70, width=3)
    pol = ImageChops.multiply(pol, Image.linear_gradient("L").rotate(180).resize((S, S)))
    biel = Image.new("RGBA", (S, S), (255, 255, 255, 0)); biel.putalpha(pol)
    warstwa = Image.alpha_composite(warstwa, biel)
    return Image.alpha_composite(ik, warstwa), maska

def symbol(rys):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    rys(ImageDraw.Draw(im), im)
    return im

B = (255, 255, 255, 255)
def tekst(d, xy, s, rozm, plik=INTER + "Inter-Bold.otf", fill=B, anchor="mm"):
    d.text(xy, s, font=ImageFont.truetype(plik, rozm), fill=fill, anchor=anchor)

def s_terminal(d, im):
    d.line((54, 70, 88, 98, 54, 126), fill=(120, 230, 160, 255), width=14, joint="curve")
    d.line((100, 128, 140, 128), fill=B, width=14)
def s_zegar(d, im):
    d.ellipse((44, 44, 148, 148), fill=B)
    d.line((96, 96, 96, 62), fill=(40, 60, 140, 255), width=10)
    d.line((96, 96, 122, 108), fill=(40, 60, 140, 255), width=10)
    d.ellipse((89, 89, 103, 103), fill=(220, 60, 60, 255))
def s_piesni(d, im):
    d.polygon([(40, 62), (92, 70), (92, 146), (40, 136)], fill=B)
    d.polygon([(152, 62), (100, 70), (100, 146), (152, 136)], fill=B)
    for i in range(4):
        y = 86 + i * 13
        d.line((50, y - 2, 84, y + 2), fill=(150, 110, 230, 255), width=4)
        d.line((108, y + 2, 142, y - 2), fill=(150, 110, 230, 255), width=4)
def s_muzyka(d, im):
    d.ellipse((46, 118, 82, 146), fill=B); d.ellipse((110, 104, 146, 132), fill=B)
    d.rectangle((72, 58, 82, 132), fill=B); d.rectangle((136, 44, 146, 118), fill=B)
    d.polygon([(72, 58), (146, 44), (146, 66), (72, 80)], fill=B)
def s_internet(d, im):
    d.ellipse((42, 42, 150, 150), outline=B, width=9)
    d.ellipse((74, 42, 118, 150), outline=B, width=8)
    d.line((46, 96, 146, 96), fill=B, width=8)
    d.arc((50, 30, 142, 90), 30, 150, fill=B, width=7)
    d.arc((50, 102, 142, 162), 210, 330, fill=B, width=7)
def s_modlitwy(d, im):
    blask = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(blask).ellipse((60, 18, 132, 98), fill=(255, 240, 180, 150))
    im.alpha_composite(blask.filter(ImageFilter.GaussianBlur(12)))
    d.rounded_rectangle((76, 86, 116, 156), 6, fill=B)
    d.polygon([(96, 30), (112, 62), (106, 78), (86, 78), (80, 62)], fill=(255, 226, 120, 255))
    d.ellipse((86, 52, 106, 80), fill=(255, 250, 220, 255))
    d.line((96, 78, 96, 88), fill=(80, 60, 40, 255), width=3)
def s_spowiedz(d, im):
    d.rounded_rectangle((40, 50, 152, 128), 26, fill=B)
    d.polygon([(70, 120), (64, 152), (100, 124)], fill=B)
    k = (110, 90, 230, 255)
    d.rectangle((91, 62, 101, 116), fill=k); d.rectangle((76, 76, 116, 86), fill=k)
def s_komunia(d, im):
    d.ellipse((72, 26, 120, 74), fill=(255, 252, 235, 255))
    d.line((96, 36, 96, 64), fill=(220, 180, 60, 255), width=4); d.line((84, 48, 108, 48), fill=(220, 180, 60, 255), width=4)
    d.chord((52, 46, 140, 130), 0, 180, fill=B)
    d.rectangle((90, 128, 102, 150), fill=B)
    d.rounded_rectangle((66, 148, 126, 160), 5, fill=B)
def s_ogloszenia(d, im):
    d.polygon([(54, 40), (124, 40), (144, 60), (144, 156), (54, 156)], fill=B)
    d.polygon([(124, 40), (124, 60), (144, 60)], fill=(170, 230, 222, 255))
    for i, dl in enumerate((70, 62, 70, 48)):
        d.line((68, 78 + i * 18, 68 + dl, 78 + i * 18), fill=(20, 150, 130, 255), width=7)
def s_msze(d, im):
    d.rectangle((50, 96, 142, 156), fill=B)
    d.polygon([(42, 100), (96, 56), (150, 100)], fill=B)
    d.rectangle((84, 52, 108, 100), fill=B)
    d.rectangle((93, 22, 99, 54), fill=B); d.rectangle((84, 32, 108, 38), fill=B)
    d.rounded_rectangle((84, 118, 108, 156), 10, fill=(80, 96, 220, 255))
def s_system(d, im):
    blask = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(blask).ellipse((40, 30, 152, 160), fill=(255, 210, 90, 110))
    im.alpha_composite(blask.filter(ImageFilter.GaussianBlur(16)))
    z = (246, 198, 64, 255)
    d.rounded_rectangle((86, 34, 106, 160), 5, fill=z); d.rounded_rectangle((56, 66, 136, 86), 5, fill=z)
def s_kalkulator(d, im):
    d.rounded_rectangle((52, 34, 140, 160), 14, fill=B)
    d.rounded_rectangle((64, 46, 128, 76), 6, fill=(30, 110, 70, 255))
    d.rectangle((95, 50, 99, 72), fill=(255, 220, 110, 255)); d.rectangle((88, 56, 106, 60), fill=(255, 220, 110, 255))
    for r in range(3):
        for c in range(3):
            col = (40, 170, 100, 255) if c == 2 else (190, 205, 198, 255)
            d.rounded_rectangle((64 + c * 23, 88 + r * 22, 80 + c * 23, 102 + r * 22), 4, fill=col)

def s_zadania(d, im):
    d.rounded_rectangle((40, 44, 152, 148), 12, outline=B, width=8)
    for i, h in enumerate((40, 64, 28, 52)):
        d.rounded_rectangle((58 + i * 22, 132 - h, 72 + i * 22, 132), 3, fill=B)
    d.line((52, 160, 140, 160), fill=(255, 255, 255, 180), width=6)

def ik_kalendarz():
    tlo, maska = kafel(hexc(0xFFFFFF), hexc(0xE6E8EE))
    pas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(pas).rectangle((0, 0, S, 72), fill=hexc(0xE5484D))
    pm = ImageChops.multiply(maska, Image.new("L", (S, S), 255))
    tlo.paste(pas, (0, 0), ImageChops.multiply(pm, pas.split()[3]))
    d = ImageDraw.Draw(tlo)
    tekst(d, (96, 48), "LIS", 26, INTER + "Inter-Bold.otf", B)
    tekst(d, (96, 122), "24", 70, INTER + "InterDisplay-SemiBold.otf", (40, 44, 56, 255))
    return tlo

IKONY = [  # kolejność jak w fb_ikona_nr
    ("terminal",   0x3A3F50, 0x1C1F29, s_terminal),
    ("kalendarz",  None, None, None),
    ("zegar",      0x5B8CFF, 0x2D52D6, s_zegar),
    ("piesni",     0xA67CFF, 0x7041DB, s_piesni),
    ("muzyka",     0xFF6A93, 0xD8325F, s_muzyka),
    ("internet",   0x45B8FF, 0x1671DE, s_internet),
    ("modlitwy",   0xFFAA55, 0xE36A12, s_modlitwy),
    ("spowiedz",   0x9586FF, 0x5A45D6, s_spowiedz),
    ("komunia",    0xF7C948, 0xCF930C, s_komunia),
    ("ogloszenia", 0x33CDB9, 0x10907D, s_ogloszenia),
    ("msze",       0x7484FF, 0x3F4ED4, s_msze),
    ("system",     0x2C4C8C, 0x121F42, s_system),
    ("kalkulator", 0x45DB93, 0x1C9A5B, s_kalkulator),
    ("zadania",    0x5E7396, 0x2E3B57, s_zadania),
]
duze = []
for nazwa, g, dl, rys in IKONY:
    if nazwa == "kalendarz": im = ik_kalendarz()
    else:
        im, _ = kafel(hexc(g), hexc(dl))
        im = Image.alpha_composite(im, symbol(rys))
    duze.append(im)
    if nazwa == "system": im.save("/tmp/ik_system.png")
arkusz = Image.new("RGBA", (S * 7, S * 2), (40, 50, 80, 255))
for i, im in enumerate(duze): arkusz.alpha_composite(im, ((i % 7) * S, (i // 7) * S))
arkusz.save("/tmp/ikony_podglad.png")

def argb(im):
    px = im.load(); w, h = im.size
    return [(px[x, y][3] << 24) | (px[x, y][0] << 16) | (px[x, y][1] << 8) | px[x, y][2] for y in range(h) for x in range(w)]
for rozm in (48, 32, 24):
    wsz = []
    for im in duze: wsz += argb(im.resize((rozm, rozm), Image.LANCZOS))
    tablica("u32", f"ikony{rozm}", wsz, 12, "0x%08X")
out.append(f"#define IKON_N {len(IKONY)}")

# ---------------------------------------------------------------- kursor

K = 4
kur = Image.new("RGBA", (24 * K, 32 * K), (0, 0, 0, 0))
pkt = [(1.5, 1.5), (1.5, 19), (5.6, 15.2), (8.2, 21.4), (11.8, 19.9), (9.3, 14.2), (14.6, 14.2)]
pkt = [(x * K, y * K) for x, y in pkt]
cien = Image.new("L", kur.size, 0)
ImageDraw.Draw(cien).polygon([(x + 1.5 * K, y + 2 * K) for x, y in pkt], fill=110)
cien = cien.filter(ImageFilter.GaussianBlur(1.6 * K))
kur.alpha_composite(Image.merge("RGBA", (Image.new("L", kur.size, 0),) * 3 + (cien,)))
d = ImageDraw.Draw(kur)
d.polygon(pkt, fill=(255, 255, 255, 255), outline=(16, 18, 26, 255), width=int(1.1 * K))
kur = kur.resize((24, 32), Image.LANCZOS)
tablica("u32", "kursor_px", argb(kur), 12, "0x%08X")
out.append("#define KURSOR_W 24\n#define KURSOR_H 32")
kur.resize((96, 128), Image.NEAREST).save("/tmp/kursor_podglad.png")

# ---------------------------------------------------------------- tapeta

TW_, TH_ = 480, 270
tap = Image.new("RGB", (TW_, TH_))
td = ImageDraw.Draw(tap)
for y in range(TH_):
    t = y / (TH_ - 1)
    a, b = (14, 24, 58), (24, 14, 46)
    td.line((0, y, TW_, y), fill=tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3)))
plamy = Image.new("RGBA", (TW_, TH_), (0, 0, 0, 0))
pd = ImageDraw.Draw(plamy)
for (x, y, r, c, a) in [(110, 70, 150, 0x2F62E0, 150), (400, 220, 170, 0x7B3FCB, 140), (330, 30, 100, 0x1FA8CC, 90),
                        (40, 250, 130, 0xC43E7E, 70), (250, 160, 90, 0x3B4FD9, 60)]:
    pd.ellipse((x - r, y - r, x + r, y + r), fill=hexc(c, a))
plamy = plamy.filter(ImageFilter.GaussianBlur(48))
tap = Image.alpha_composite(tap.convert("RGBA"), plamy)
# promienie światła z góry
prom = Image.new("L", (TW_, TH_), 0)
prd = ImageDraw.Draw(prom)
for k in range(-5, 6):
    kat = math.radians(90 + k * 9)
    x2, y2 = 330 + math.cos(kat) * 600, -40 + math.sin(kat) * 600
    prd.polygon([(330, -40), (x2 - 18, y2), (x2 + 18, y2)], fill=16)
prom = prom.filter(ImageFilter.GaussianBlur(10))
tap = Image.alpha_composite(tap, Image.merge("RGBA", (Image.new("L", (TW_, TH_), 255),) * 3 + (prom,)))
# złota poświata za krzyżem (sam krzyż system rysuje ostro w pełnej rozdzielczości)
kr = Image.new("L", (TW_, TH_), 0)
krd = ImageDraw.Draw(kr)
krd.rounded_rectangle((324, 50, 336, 152), 4, fill=150); krd.rounded_rectangle((298, 76, 362, 88), 4, fill=150)
kr = kr.filter(ImageFilter.GaussianBlur(12))
tap = Image.alpha_composite(tap, Image.merge("RGBA", (Image.new("L", (TW_, TH_), 255), Image.new("L", (TW_, TH_), 214),
                                                     Image.new("L", (TW_, TH_), 140), kr)))
tap = tap.convert("RGB")
tap.resize((1920, 1080), Image.BILINEAR).save("/tmp/tapeta_podglad.png")
tablica("u8", "tapeta_rgb", list(tap.tobytes()), 48)
out.append(f"#define TAPETA_W {TW_}\n#define TAPETA_H {TH_}")

open(WYJ, "w").write("\n".join(out) + "\n")
print("zapisano", WYJ, os.path.getsize(WYJ) // 1024, "KB")
