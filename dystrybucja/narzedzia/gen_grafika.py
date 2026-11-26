#!/usr/bin/env python3
"""Grafika dystrybucji AmenOS (na bazie Lubuntu):
 - ikony programów parafialnych (256 px)          -> aplikacje/wspolne/ikony/*.png
 - tapeta 3840x2160 i 1920x1080                   -> dystrybucja/wyglad/tapeta*.jpg
 - logo ekranu startowego (Plymouth) i kropka      -> dystrybucja/wyglad/plymouth/
 - tło menu GRUB na płycie                         -> dystrybucja/wyglad/grub-tlo.png
 - obrazy instalatora (Calamares)                  -> dystrybucja/wyglad/calamares/

Użycie (z głównego folderu repozytorium): python3 dystrybucja/narzedzia/gen_grafika.py
Potrzebne: Pillow, czcionki Inter (fonts-inter) i DejaVu."""
import os, math, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

KORZEN = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
WYGLAD = os.path.join(KORZEN, "dystrybucja", "wyglad")
IKONY = os.path.join(KORZEN, "aplikacje", "wspolne", "ikony")
INTER = "/usr/share/fonts/opentype/inter/"
DEJAVU = "/usr/share/fonts/truetype/dejavu/"
for d in (WYGLAD, IKONY, os.path.join(WYGLAD, "plymouth"), os.path.join(WYGLAD, "calamares")):
    os.makedirs(d, exist_ok=True)

def hexc(h, a=255): return ((h >> 16) & 255, (h >> 8) & 255, h & 255, a)
def font(nazwa, rozm): return ImageFont.truetype(INTER + nazwa, rozm)

# ---------------------------------------------------------------- ikony

S = 768                       # rysujemy w powiększeniu (4 x 192 jak w jądrze), potem zmniejszamy
K = S / 192                   # współczynnik względem rysunków z jądra AmenOS
B = (255, 255, 255, 255)

def sk(*xy): return [v * K for v in xy]

def kafel(gora, dol):
    pad, r = 10 * K, 44 * K
    maska = Image.new("L", (S, S), 0)
    ImageDraw.Draw(maska).rounded_rectangle((pad, pad, S - pad, S - pad), r, fill=255)
    grad = Image.new("RGBA", (S, S))
    gd = ImageDraw.Draw(grad)
    for y in range(S):
        t = y / (S - 1)
        gd.line((0, y, S, y), fill=tuple(int(gora[i] * (1 - t) + dol[i] * t) for i in range(3)) + (255,))
    cm = Image.new("L", (S, S), 0)
    ImageDraw.Draw(cm).rounded_rectangle((pad, pad + 6 * K, S - pad, S - pad + 6 * K), r, fill=90)
    cien = Image.new("RGBA", (S, S), (0, 0, 0, 0)); cien.putalpha(cm.filter(ImageFilter.GaussianBlur(6 * K)))
    w = Image.new("RGBA", (S, S), (0, 0, 0, 0)); w.paste(grad, (0, 0), maska)
    pol = Image.new("L", (S, S), 0)
    ImageDraw.Draw(pol).rounded_rectangle((pad + 2 * K, pad + 2 * K, S - pad - 2 * K, S - pad - 2 * K), r - 2 * K, outline=70, width=int(3 * K))
    pol = ImageChops.multiply(pol, Image.linear_gradient("L").rotate(180).resize((S, S)))
    biel = Image.new("RGBA", (S, S), (255, 255, 255, 0)); biel.putalpha(pol)
    return Image.alpha_composite(cien, Image.alpha_composite(w, biel)), maska

def blask(im, box, kolor, rozmycie):
    b = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(b).ellipse(sk(*box), fill=kolor)
    im.alpha_composite(b.filter(ImageFilter.GaussianBlur(rozmycie * K)))

def s_piesni(d, im):
    d.polygon([tuple(sk(40, 62)), tuple(sk(92, 70)), tuple(sk(92, 146)), tuple(sk(40, 136))], fill=B)
    d.polygon([tuple(sk(152, 62)), tuple(sk(100, 70)), tuple(sk(100, 146)), tuple(sk(152, 136))], fill=B)
    for i in range(4):
        y = 86 + i * 13
        d.line(sk(50, y - 2, 84, y + 2), fill=(150, 110, 230, 255), width=int(4 * K))
        d.line(sk(108, y + 2, 142, y - 2), fill=(150, 110, 230, 255), width=int(4 * K))
def s_muzyka(d, im):
    d.ellipse(sk(46, 118, 82, 146), fill=B); d.ellipse(sk(110, 104, 146, 132), fill=B)
    d.rectangle(sk(72, 58, 82, 132), fill=B); d.rectangle(sk(136, 44, 146, 118), fill=B)
    d.polygon([tuple(sk(72, 58)), tuple(sk(146, 44)), tuple(sk(146, 66)), tuple(sk(72, 80))], fill=B)
def s_modlitwy(d, im):
    blask(im, (60, 18, 132, 98), (255, 240, 180, 150), 12)
    d.rounded_rectangle(sk(76, 86, 116, 156), 6 * K, fill=B)
    d.polygon([tuple(sk(96, 30)), tuple(sk(112, 62)), tuple(sk(106, 78)), tuple(sk(86, 78)), tuple(sk(80, 62))], fill=(255, 226, 120, 255))
    d.ellipse(sk(86, 52, 106, 80), fill=(255, 250, 220, 255))
    d.line(sk(96, 78, 96, 88), fill=(80, 60, 40, 255), width=int(3 * K))
def s_spowiedz(d, im):
    d.rounded_rectangle(sk(40, 50, 152, 128), 26 * K, fill=B)
    d.polygon([tuple(sk(70, 120)), tuple(sk(64, 152)), tuple(sk(100, 124))], fill=B)
    k = (110, 90, 230, 255)
    d.rectangle(sk(91, 62, 101, 116), fill=k); d.rectangle(sk(76, 76, 116, 86), fill=k)
def s_komunia(d, im):
    d.ellipse(sk(72, 26, 120, 74), fill=(255, 252, 235, 255))
    d.line(sk(96, 36, 96, 64), fill=(220, 180, 60, 255), width=int(4 * K)); d.line(sk(84, 48, 108, 48), fill=(220, 180, 60, 255), width=int(4 * K))
    d.chord(sk(52, 46, 140, 130), 0, 180, fill=B)
    d.rectangle(sk(90, 128, 102, 150), fill=B)
    d.rounded_rectangle(sk(66, 148, 126, 160), 5 * K, fill=B)
def s_ogloszenia(d, im):
    d.polygon([tuple(sk(54, 40)), tuple(sk(124, 40)), tuple(sk(144, 60)), tuple(sk(144, 156)), tuple(sk(54, 156))], fill=B)
    d.polygon([tuple(sk(124, 40)), tuple(sk(124, 60)), tuple(sk(144, 60))], fill=(170, 230, 222, 255))
    for i, dl in enumerate((70, 62, 70, 48)):
        d.line(sk(68, 78 + i * 18, 68 + dl, 78 + i * 18), fill=(20, 150, 130, 255), width=int(7 * K))
def s_msze(d, im):
    d.rectangle(sk(50, 96, 142, 156), fill=B)
    d.polygon([tuple(sk(42, 100)), tuple(sk(96, 56)), tuple(sk(150, 100))], fill=B)
    d.rectangle(sk(84, 52, 108, 100), fill=B)
    d.rectangle(sk(93, 22, 99, 54), fill=B); d.rectangle(sk(84, 32, 108, 38), fill=B)
    d.rounded_rectangle(sk(84, 118, 108, 156), 10 * K, fill=(80, 96, 220, 255))
def s_krzyz(d, im):
    blask(im, (40, 30, 152, 160), (255, 210, 90, 110), 16)
    z = (246, 198, 64, 255)
    d.rounded_rectangle(sk(86, 34, 106, 160), 5 * K, fill=z); d.rounded_rectangle(sk(56, 66, 136, 86), 5 * K, fill=z)
def s_kalkulator(d, im):
    d.rounded_rectangle(sk(52, 34, 140, 160), 14 * K, fill=B)
    d.rounded_rectangle(sk(64, 46, 128, 76), 6 * K, fill=(30, 110, 70, 255))
    d.rectangle(sk(95, 50, 99, 72), fill=(255, 220, 110, 255)); d.rectangle(sk(88, 56, 106, 60), fill=(255, 220, 110, 255))
    for r in range(3):
        for c in range(3):
            d.rounded_rectangle(sk(64 + c * 23, 88 + r * 22, 80 + c * 23, 102 + r * 22), 4 * K, fill=(40, 170, 100, 255) if c == 2 else (190, 205, 198, 255))
def s_dzis(d, im):
    blask(im, (40, 70, 152, 170), (255, 236, 170, 140), 14)
    d.pieslice(sk(52, 86, 140, 174), 180, 360, fill=(255, 246, 214, 255))
    for k in range(7):
        a = math.radians(180 + 15 + k * 25)
        d.line(sk(96 + math.cos(a) * 52, 130 + math.sin(a) * 52, 96 + math.cos(a) * 70, 130 + math.sin(a) * 70), fill=B, width=int(6 * K))
    d.rectangle(sk(40, 128, 152, 136), fill=B)
    z = (255, 255, 255, 255)
    d.rounded_rectangle(sk(92, 26, 100, 72), 2 * K, fill=z); d.rounded_rectangle(sk(80, 38, 112, 46), 2 * K, fill=z)

def ik_kalendarz():
    tlo, maska = kafel(hexc(0xFFFFFF), hexc(0xE6E8EE))
    pas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(pas).rectangle((0, 0, S, 72 * K), fill=hexc(0xE5484D))
    tlo.paste(pas, (0, 0), ImageChops.multiply(maska, pas.split()[3]))
    d = ImageDraw.Draw(tlo)
    d.text(sk(96, 48), "LITURGIA", font=font("Inter-Bold.otf", int(17 * K)), fill=B, anchor="mm")
    d.text(sk(96, 122), "31", font=font("InterDisplay-SemiBold.otf", int(70 * K)), fill=(40, 44, 56, 255), anchor="mm")
    return tlo

IKONY_DEF = {
    "amenos":      (0x2C4C8C, 0x121F42, s_krzyz),
    "dzis":        (0xFFA35C, 0xE0526E, s_dzis),
    "piesni":      (0xA67CFF, 0x7041DB, s_piesni),
    "modlitewnik": (0xFFAA55, 0xE36A12, s_modlitwy),
    "spowiedz":    (0x9586FF, 0x5A45D6, s_spowiedz),
    "komunia":     (0xF7C948, 0xCF930C, s_komunia),
    "ogloszenia":  (0x33CDB9, 0x10907D, s_ogloszenia),
    "msze":        (0x7484FF, 0x3F4ED4, s_msze),
    "kalkulator":  (0x45DB93, 0x1C9A5B, s_kalkulator),
    "muzyka":      (0xFF6A93, 0xD8325F, s_muzyka),
}
podglad = []
for nazwa, (g, dl, rys) in IKONY_DEF.items():
    im, _ = kafel(hexc(g), hexc(dl))
    sym = Image.new("RGBA", (S, S), (0, 0, 0, 0)); rys(ImageDraw.Draw(sym), sym)
    im = Image.alpha_composite(im, sym).resize((256, 256), Image.LANCZOS)
    im.save(os.path.join(IKONY, nazwa + ".png")); podglad.append(im)
im = ik_kalendarz().resize((256, 256), Image.LANCZOS); im.save(os.path.join(IKONY, "kalendarz.png")); podglad.append(im)
ark = Image.new("RGBA", (256 * len(podglad), 256), (30, 36, 60, 255))
for i, p in enumerate(podglad): ark.alpha_composite(p, (i * 256, 0))
ark.save("/tmp/amenos-ikony.png")

# ---------------------------------------------------------------- tapeta

def tapeta(W, H):
    w, h = 960, 540
    tap = Image.new("RGB", (w, h))
    td = ImageDraw.Draw(tap)
    for y in range(h):
        t = y / (h - 1)
        a, b = (14, 24, 58), (24, 14, 46)
        td.line((0, y, w, y), fill=tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3)))
    pl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pd = ImageDraw.Draw(pl)
    for (x, y, r, c, a) in [(220, 140, 300, 0x2F62E0, 150), (800, 440, 340, 0x7B3FCB, 140), (660, 60, 200, 0x1FA8CC, 90),
                            (80, 500, 260, 0xC43E7E, 70), (500, 320, 180, 0x3B4FD9, 60)]:
        pd.ellipse((x - r, y - r, x + r, y + r), fill=hexc(c, a))
    tap = Image.alpha_composite(tap.convert("RGBA"), pl.filter(ImageFilter.GaussianBlur(96)))
    pr = Image.new("L", (w, h), 0)
    prd = ImageDraw.Draw(pr)
    for k in range(-5, 6):
        kat = math.radians(90 + k * 9)
        x2, y2 = 660 + math.cos(kat) * 1200, -80 + math.sin(kat) * 1200
        prd.polygon([(660, -80), (x2 - 36, y2), (x2 + 36, y2)], fill=16)
    pr = pr.filter(ImageFilter.GaussianBlur(20))
    tap = Image.alpha_composite(tap, Image.merge("RGBA", (Image.new("L", (w, h), 255),) * 3 + (pr,)))
    kr = Image.new("L", (w, h), 0)
    krd = ImageDraw.Draw(kr)
    krd.rounded_rectangle((648, 100, 672, 304), 8, fill=150); krd.rounded_rectangle((596, 152, 724, 176), 8, fill=150)
    tap = Image.alpha_composite(tap, Image.merge("RGBA", (Image.new("L", (w, h), 255), Image.new("L", (w, h), 214),
                                                         Image.new("L", (w, h), 140), kr.filter(ImageFilter.GaussianBlur(24)))))
    tap = tap.resize((W, H), Image.BICUBIC)
    # ostry krzyż w pełnej rozdzielczości
    kr = Image.new("L", (W, H), 0)
    krd = ImageDraw.Draw(kr)
    cx, sz = W * 660 // 960, max(3, W // 240)
    krd.rounded_rectangle((cx - sz // 2, H * 104 // 540, cx + sz // 2, H * 300 // 540), sz // 2, fill=100)
    krd.rounded_rectangle((cx - W * 60 // 960, H * 164 // 540 - sz // 2, cx + W * 60 // 960, H * 164 // 540 + sz // 2), sz // 2, fill=100)
    tap = Image.alpha_composite(tap, Image.merge("RGBA", (Image.new("L", (W, H), 255), Image.new("L", (W, H), 244), Image.new("L", (W, H), 214), kr)))
    # odrobina ziarna przeciw pasmom w gradientach
    random.seed(1)
    szum = Image.effect_noise((W, H), 6).point(lambda v: v - 128 + 128)
    tap = ImageChops.add(tap.convert("RGB"), Image.merge("RGB", (szum,) * 3), 1.0, -128)
    return tap

t4k = tapeta(3840, 2160); t4k.save(os.path.join(WYGLAD, "tapeta.jpg"), quality=92)
thd = t4k.resize((1920, 1080), Image.LANCZOS); thd.save(os.path.join(WYGLAD, "tapeta-1920.jpg"), quality=92)
thd.resize((960, 540)).save("/tmp/amenos-tapeta.png")

# ---------------------------------------------------------------- Plymouth: logo i kropka

logo = Image.new("RGBA", (720, 420), (0, 0, 0, 0))
blaskl = Image.new("RGBA", logo.size, (0, 0, 0, 0))
ImageDraw.Draw(blaskl).ellipse((260, 10, 460, 230), fill=(246, 198, 64, 90))
logo.alpha_composite(blaskl.filter(ImageFilter.GaussianBlur(30)))
d = ImageDraw.Draw(logo)
z = (246, 198, 64, 255)
d.rounded_rectangle((347, 30, 373, 210), 7, fill=z); d.rounded_rectangle((300, 82, 420, 108), 7, fill=z)
d.text((360, 300), "AmenOS", font=font("InterDisplay-Light.otf", 96), fill=(255, 255, 255, 255), anchor="mm")
d.text((360, 380), "system dla parafii", font=font("Inter-Regular.otf", 26), fill=(184, 198, 232, 255), anchor="mm")
logo.save(os.path.join(WYGLAD, "plymouth", "logo.png"))
kr = Image.new("RGBA", (24, 24), (0, 0, 0, 0))
ImageDraw.Draw(kr).ellipse((2, 2, 22, 22), fill=(255, 255, 255, 255))
kr.save(os.path.join(WYGLAD, "plymouth", "kropka.png"))

# ---------------------------------------------------------------- GRUB i Calamares

g = thd.convert("RGBA")
g = Image.alpha_composite(g, Image.new("RGBA", g.size, (5, 8, 26, 110)))
# bez logo na środku - menu GRUB zajmuje całą szerokość ekranu; krzyż z tapety zostaje po prawej
g.convert("RGB").save(os.path.join(WYGLAD, "grub-tlo.png"))

CAL = os.path.join(WYGLAD, "calamares")
Image.open(os.path.join(IKONY, "amenos.png")).resize((128, 128), Image.LANCZOS).save(os.path.join(CAL, "logo.png"))
pow_ = thd.resize((800, 450), Image.LANCZOS).convert("RGBA")
pow_.alpha_composite(logo.resize((360, 210), Image.LANCZOS), (220, 110))
pow_.convert("RGB").save(os.path.join(CAL, "powitanie.png"))
SLAJDY = [
    ("Witaj w AmenOS", "System dla parafii zbudowany na Lubuntu 26.04 LTS.\nInstalacja potrwa kilka minut."),
    ("Programy parafialne", "Kalendarz liturgiczny, tablica pieśni, modlitewnik\nz różańcem, ogłoszenia i porządek Mszy."),
    ("Na wszystko jest czas", "Spowiedź, Komunia święta i muzyka mszalna na organach.\nA w kalkulatorze dzielenie przez zero kończy się „A-men”."),
    ("Lubuntu pod spodem", "Pełny Linux: przeglądarka Falkon, LibreOffice, drukarki,\nWi-Fi, HDMI i UEFI - wszystko działa od razu."),
]
for i, (tyt, opis) in enumerate(SLAJDY, 1):
    sl = thd.resize((800, 440), Image.LANCZOS).convert("RGBA")
    sl = Image.alpha_composite(sl, Image.new("RGBA", sl.size, (5, 8, 26, 90)))
    sd = ImageDraw.Draw(sl)
    ik = ["amenos", "kalendarz", "spowiedz", "dzis"][i - 1]
    sl.alpha_composite(Image.open(os.path.join(IKONY, ik + ".png")).resize((120, 120), Image.LANCZOS), (340, 60))
    sd.text((400, 230), tyt, font=font("InterDisplay-SemiBold.otf", 40), fill=(255, 255, 255, 255), anchor="mm")
    sd.multiline_text((400, 320), opis, font=font("Inter-Regular.otf", 21), fill=(214, 222, 240, 255), anchor="mm", align="center", spacing=10)
    sl.convert("RGB").save(os.path.join(CAL, f"slajd{i}.png"))
print("gotowe:", IKONY, WYGLAD)
