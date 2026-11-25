/* AmenOS - pulpit graficzny: okna, ikony, pasek zadań, menu Start i programy graficzne.
 *
 * System ma jeden "wątek": powłokę tekstową. Pulpit działa przy okazji - za każdym razem,
 * gdy jakikolwiek program czeka na klawisz (kbd_poll) albo odlicza czas (czekaj_ms),
 * wołana jest gui_pompuj(), która obsługuje mysz, okna i rysowanie. Powłoka i wszystkie
 * programy tekstowe działają w oknie Terminal.
 *
 * Wygląd: wygładzone czcionki, okna z zaokrąglonymi rogami i miękkim cieniem, półprzezroczysty
 * pasek zadań z ikonami pośrodku, kolor akcentu = kolor liturgiczny dnia. */
#include "kernel.h"
#include "muzyka_dane.h"
#include "apki.h"

int gui_aktywne;
int powloka_czeka;

#define PASEK_H 52                         /* pasek zadań */
#define TB      40                         /* belka tytułowa okna */
#define BR      1                          /* ramka okna */
#define R_OKNA  10                         /* promień zaokrąglenia okien */
#define PRZ_B   46                         /* szerokość przycisków na belce */
#define TPX     12                         /* margines terminala */
#define TPY     10

static u32 pamiec_kb;

/* ---------- kolory ---------- */

static const u32 PALETA16[16] = { 0x0C0D12, 0x2F62D9, 0x18A34A, 0x0EA5C6, 0xD6333F, 0x9B3FBF, 0xC79A16, 0xCDD1D8,
                                  0x6B7280, 0x6B95FF, 0x4ADE80, 0x5EEAD4, 0xFF6B6B, 0xE879F9, 0xFDE047, 0xF8FAFC };
static u32 kolor_lit(int k) {
    static const u32 t[] = { 0x2F9E5B, 0x7D4CC9, 0xF3EEE0, 0xD23B3B, 0xE07BA8 };
    return t[k];
}
static u32 akcent_lit(int k) {
    static const u32 t[] = { 0x23874B, 0x7046C2, 0xB8901C, 0xC8343A, 0xD0628F };
    return t[k];
}

#define TLO_OKNA   0xFAFBFC
#define BELKA      0xEEF1F5
#define BELKA_NA   0xF5F6F8
#define TEKST      0x1B1F2A
#define SZARY_T    0x6B7280
#define LINIA      0xE2E5EB
#define TERM_TLO   0x0C0D12
#define TERM_BELKA 0x1E2029

static czas_t dzis;
static dzien_lit_t dzis_lit;
static u32 akcent(void) { return akcent_lit(dzis_lit.kolor); }

/* ---------- okna ---------- */

enum { O_TERMINAL, O_KALENDARZ, O_ZEGAR, O_MUZYKA, O_INFO, O_WWW, O_KALK, O_APKA, O_KOMUNIKAT = O_APKA + AP_N, O_N };
#define JEST_APKA(i) ((i) >= O_APKA && (i) < O_APKA + AP_N)
typedef struct { int x, y, cw, ch; const char *tytul; int ikona, otwarte, zminim; } okno_t;
static okno_t okna[O_N] = {        /* okna programów z aplikacje.c (O_APKA...) uzupełnia gui_init */
    [O_TERMINAL]  = { 0, 0, 80 * TERM_W + 2 * TPX, 25 * TERM_H + 2 * TPY, "Terminal", 0, 0, 0 },
    [O_KALENDARZ] = { 0, 0, 380, 470, "Kalendarz liturgiczny", 1, 0, 0 },
    [O_ZEGAR]     = { 0, 0, 300, 380, "Zegar",                 2, 0, 0 },
    [O_MUZYKA]    = { 0, 0, 500, 400, "Muzyka mszalna",        4, 0, 0 },
    [O_INFO]      = { 0, 0, 500, 296, "O systemie",           11, 0, 0 },
    [O_WWW]       = { 0, 0, 960, 700, "Przeglądarka",          5, 0, 0 },
    [O_KALK]      = { 0, 0, 326, 540, "Kalkulator biblijny",  12, 0, 0 },
    [O_KOMUNIKAT] = { 0, 0, 440, 160, "AmenOS",               11, 0, 0 },
};
static int z[O_N], zn;                      /* kolejność od spodu do wierzchu */

static int bx0, by0, bx1, by1, brud;        /* obszar do przerysowania */
static int mx, my, przyc;                   /* mysz */
static int ciagniete = -1, cdx, cdy;        /* przeciągane okno */
static int menu_otwarte;
static int wybrana_ikona = -1;
static u32 ost_klik_czas;
static char komunikat_txt[200];

static int klient_ciag = -1;                /* okno, w którym trzymamy przycisk myszy */
static int www_pod = 0;

/* najechanie myszą (podświetlenia) */
enum { H_BRAK, H_ZAMKNIJ, H_MINIM, H_PASEK, H_MENU, H_IKONA, H_MENU_DOL, H_LIT, H_GLOSNIK };
static int hov_typ, hov_id, hov_x, hov_y, hov_w, hov_h;

/* przeglądarka graficzna (www_gui.c) */
int  wwwg_init(int cw, int ch);
void wwwg_rysuj(int x, int y);
void wwwg_klik(int lx, int ly);
void wwwg_ciagnij(int lx, int ly);
void wwwg_puszczenie(void);
int  wwwg_najedz(int lx, int ly);
void wwwg_klawisz(int k);
void wwwg_tik(void);
void wwwg_otworz(const char *url);
const char *wwwg_tytul(void);
static int www_start;

/* kalkulator biblijny (kalkulator.c) */
void kalk_init(int cw, int ch);
void kalk_rysuj(int x, int y);
void kalk_klik(int lx, int ly);
void kalk_puszczenie(void);
void kalk_klawisz(int k);
int  kalk_szer(void);
int  kalk_wys(void);
void gui_kalk_brudne(int x, int y, int w, int h);

static void brudne(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!brud) { bx0 = x; by0 = y; bx1 = x + w; by1 = y + h; brud = 1; return; }
    if (x < bx0) bx0 = x;
    if (y < by0) by0 = y;
    if (x + w > bx1) bx1 = x + w;
    if (y + h > by1) by1 = y + h;
}
static void brudne_wszystko(void) { brudne(0, 0, fb_w, fb_h); }
static void brudny_pasek(void) { brudne(0, fb_h - PASEK_H, fb_w, PASEK_H); }

static int ow(int i) { return okna[i].cw + 2 * BR; }
static int oh(int i) { return okna[i].ch + TB + BR; }
static int widoczne(int i) { return okna[i].otwarte && !okna[i].zminim; }
static void okno_brudne(int i) { brudne(okna[i].x - 26, okna[i].y - 18, ow(i) + 52, oh(i) + 52); }

static int okno_aktywne(void) {
    for (int k = zn - 1; k >= 0; k--) if (widoczne(z[k])) return z[k];
    return -1;
}

static void okno_podnies(int i) {
    int k = 0;
    while (k < zn && z[k] != i) k++;
    if (k == zn) { z[zn++] = i; }
    else { int stare = okno_aktywne(); for (; k < zn - 1; k++) z[k] = z[k + 1]; z[zn - 1] = i; if (stare >= 0) okno_brudne(stare); }
    okno_brudne(i);
    brudny_pasek();
}

static void okno_otworz(int i) {
    int stare = okno_aktywne();
    if (JEST_APKA(i) && !okna[i].otwarte) apki[i - O_APKA].otwarte();
    okna[i].otwarte = 1;
    okna[i].zminim = 0;
    okno_podnies(i);
    if (stare >= 0) okno_brudne(stare);
}

static void muzyka_stop(void);

static void okno_zamknij(int i) {
    okno_brudne(i);
    okna[i].otwarte = 0;
    int k = 0;
    while (k < zn && z[k] != i) k++;
    for (; k < zn - 1; k++) z[k] = z[k + 1];
    if (zn && k <= zn - 1) zn--;
    if (i == O_MUZYKA) muzyka_stop();
    if (JEST_APKA(i)) apki[i - O_APKA].zamkniete();
    int a = okno_aktywne();
    if (a >= 0) okno_brudne(a);
    brudny_pasek();
}

static void okno_minimalizuj(int i) {
    okna[i].zminim = 1; okno_brudne(i);
    int a = okno_aktywne();
    if (a >= 0) okno_brudne(a);
    brudny_pasek();
}

static void komunikat(const char *s) {
    k_strncpy(komunikat_txt, s, sizeof komunikat_txt);
    okna[O_KOMUNIKAT].x = (fb_w - ow(O_KOMUNIKAT)) / 2;
    okna[O_KOMUNIKAT].y = (fb_h - PASEK_H - oh(O_KOMUNIKAT)) / 2;
    okno_otworz(O_KOMUNIKAT);
    dzw_efekt(E_POWIADOM);
}

int gui_terminal_aktywny(void) { return !menu_otwarte && okno_aktywne() == O_TERMINAL; }

void gui_www_brudne(int x, int y, int w, int h) {
    if (widoczne(O_WWW)) brudne(okna[O_WWW].x + BR + x, okna[O_WWW].y + TB + y, w, h);
    if (y < 0 && okna[O_WWW].otwarte) brudny_pasek();
}
static void komponuj(void);
void gui_rysuj_teraz(void) { if (brud) komponuj(); }
void gui_kalk_brudne(int x, int y, int w, int h) {
    if (widoczne(O_KALK)) brudne(okna[O_KALK].x + BR + x, okna[O_KALK].y + TB + y, w, h);
}
int gui_www_aktywne(void) { return !menu_otwarte && okno_aktywne() == O_WWW; }

/* ---------- elementy wspólne ---------- */

static int w_prost(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

static void przycisk(int x, int y, int w, int h, const char *t, int wazny) {
    if (wazny) fb_zaokr(x, y, w, h, 8, akcent(), 256);
    else { fb_zaokr(x, y, w, h, 8, 0xFFFFFF, 256); fb_zaokr_ramka(x, y, w, h, 8, 0x000000, 40); }
    int f = F_TEKST_B;
    fb_napis(x + (w - fb_napis_szer(t, f)) / 2, y + (h - fb_napis_wys(f)) / 2, t, wazny ? 0xFFFFFF : TEKST, f);
}

static void szewron(int cx, int cy, int kier, u32 c) {          /* < albo > */
    fb_linia_aa(cx + 3 * kier, cy - 6, cx - 3 * kier, cy, 2, c);
    fb_linia_aa(cx - 3 * kier, cy, cx + 3 * kier, cy + 6, 2, c);
}

/* ====================================================================== */
/*  Terminal                                                               */
/* ====================================================================== */

static u16 term_pokazane[VGA_W * VGA_H];
static int kur_x = -1, kur_y = -1, kur_stan;

static void rysuj_terminal(int cx, int cy) {
    int kx, ky, kw, kh;
    cx += TPX; cy += TPY;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    int c0 = (kx - cx) / TERM_W, c1 = (kx + kw - cx + TERM_W - 1) / TERM_W, r0 = (ky - cy) / TERM_H, r1 = (ky + kh - cy + TERM_H - 1) / TERM_H;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > VGA_W) c1 = VGA_W;
    if (r1 > VGA_H) r1 = VGA_H;
    const u16 *b = vga_bufor();
    for (int r = r0; r < r1; r++)
        for (int c = c0; c < c1; c++) {
            u16 v = b[r * VGA_W + c];
            fb_term_znak(cx + c * TERM_W, cy + r * TERM_H, (u8)v, PALETA16[(v >> 8) & 15], PALETA16[(v >> 12) & 15]);
        }
    if (kur_stan && kur_x >= 0) {
        u16 v = b[kur_y * VGA_W + kur_x];
        fb_prost(cx + kur_x * TERM_W, cy + kur_y * TERM_H + 17, TERM_W, 2, PALETA16[(v >> 8) & 15]);
    }
}

static void terminal_sprawdz(u32 t) {
    const u16 *b = vga_bufor();
    int c0 = VGA_W, c1 = -1, r0 = VGA_H, r1 = -1;
    for (int i = 0; i < VGA_W * VGA_H; i++)
        if (b[i] != term_pokazane[i]) {
            term_pokazane[i] = b[i];
            int r = i / VGA_W, c = i % VGA_W;
            if (r < r0) r0 = r;
            if (r > r1) r1 = r;
            if (c < c0) c0 = c;
            if (c > c1) c1 = c;
        }
    int ox = okna[O_TERMINAL].x + BR + TPX, oy = okna[O_TERMINAL].y + TB + TPY;
    int widac = widoczne(O_TERMINAL);
    if (r1 >= 0 && widac) brudne(ox + c0 * TERM_W, oy + r0 * TERM_H, (c1 - c0 + 1) * TERM_W, (r1 - r0 + 1) * TERM_H);
    int stan = widac && gui_terminal_aktywny() && vga_kursor_widoczny() && ((t / 500) & 1) == 0;
    int nx = vga_x(), ny = vga_y();
    if (nx >= VGA_W) nx = VGA_W - 1;
    if (ny >= VGA_H) ny = VGA_H - 1;
    if (stan != kur_stan || nx != kur_x || ny != kur_y) {
        if (kur_x >= 0) brudne(ox + kur_x * TERM_W, oy + kur_y * TERM_H, TERM_W, TERM_H);
        kur_stan = stan; kur_x = nx; kur_y = ny;
        brudne(ox + kur_x * TERM_W, oy + kur_y * TERM_H, TERM_W, TERM_H);
    }
}

static void polecenie(const char *cmd) {
    okno_otworz(O_TERMINAL);
    if (!powloka_czeka) {
        komunikat("Terminal jest zajęty. Zakończ najpierw bieżący program (zwykle klawiszem Esc) i spróbuj ponownie.");
        return;
    }
    powloka_czeka = 0;
    kbd_wstrzyknij(cmd);
    kbd_wstrzyknij("\n");
}

/* ====================================================================== */
/*  Kalendarz liturgiczny                                                  */
/* ====================================================================== */

static int kal_r, kal_m, kal_d;
#define KAL_X0 15
#define KAL_Y0 92
#define KAL_KW 50
#define KAL_KH 44

static void rysuj_kalendarz(int x, int y) {
    int w = okna[O_KALENDARZ].cw;
    dzien_lit_t wl;
    liturgia(kal_r, kal_m, kal_d, &wl);
    char t[120];
    ksprintf(t, sizeof t, "%s %d", mies_mian[kal_m], kal_r);
    fb_napis(x + 20, y + 18, t, TEKST, F_SREDNI);
    /* przyciski miesięcy */
    int bx = x + w - 92;
    fb_kolo_aa(bx + 18, y + 30, 16, hov_typ == H_LIT && hov_id == 10 ? 0xE4E7ED : 0xF0F2F5, 256);
    fb_kolo_aa(bx + 58, y + 30, 16, hov_typ == H_LIT && hov_id == 11 ? 0xE4E7ED : 0xF0F2F5, 256);
    szewron(bx + 18, y + 30, 1, TEKST);
    szewron(bx + 58, y + 30, -1, TEKST);

    static const char *nagl[7] = { "Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd" };
    for (int i = 0; i < 7; i++) fb_napis_srodek(x + KAL_X0 + i * KAL_KW, KAL_KW, y + 64, nagl[i], i == 6 ? 0xC8343A : SZARY_T, F_MALY);

    int pierwszy = dni_od_epoki(kal_r, kal_m, 1);
    int kol = (dzien_tygodnia(pierwszy) + 6) % 7, wiersz = 0;
    for (int d = 1; d <= dni_w_miesiacu(kal_r, kal_m); d++) {
        dzien_lit_t l; liturgia(kal_r, kal_m, d, &l);
        int cx = x + KAL_X0 + kol * KAL_KW, cy = y + KAL_Y0 + wiersz * KAL_KH;
        int dzisiaj = d == dzis.dzien && kal_m == dzis.mies && kal_r == dzis.rok;
        u32 tlo = dzisiaj ? akcent() : (d == kal_d ? 0xE6EBF3 : 0xFFFFFF);
        fb_zaokr(cx + 3, cy + 2, KAL_KW - 6, KAL_KH - 4, 9, tlo, 256);
        if (d == kal_d && !dzisiaj) fb_zaokr_ramka(cx + 3, cy + 2, KAL_KW - 6, KAL_KH - 4, 9, 0x2B3550, 200);
        else if (!dzisiaj) fb_zaokr_ramka(cx + 3, cy + 2, KAL_KW - 6, KAL_KH - 4, 9, 0x000000, 22);
        char n[4]; ksprintf(n, sizeof n, "%d", d);
        int f = l.ranga <= 2 ? F_TEKST_B : F_TEKST;
        u32 kt = dzisiaj ? 0xFFFFFF : (kol == 6 || l.ranga <= 1 ? 0xB42A32 : TEKST);
        fb_napis_srodek(cx, KAL_KW, cy + 6, n, kt, f);
        fb_kolo_aa(cx + KAL_KW / 2, cy + 32, 4, kolor_lit(l.kolor), 256);
        if (l.kolor == KOL_BIALY) fb_okrag_aa(cx + KAL_KW / 2, cy + 32, 4, 1, 0x9A9384, 256);
        else if (dzisiaj) fb_okrag_aa(cx + KAL_KW / 2, cy + 32, 5, 1, 0xFFFFFF, 256);
        if (++kol == 7) { kol = 0; wiersz++; }
    }

    int iy = y + KAL_Y0 + 6 * KAL_KH + 8;
    fb_prost(x + 20, iy - 4, w - 40, 1, LINIA);
    int dow = dzien_tygodnia(dni_od_epoki(kal_r, kal_m, kal_d));
    ksprintf(t, sizeof t, "%s, %d %s %d", dni_tyg_wielk[dow], kal_d, mies_dop[kal_m], kal_r);
    fb_napis(x + 20, iy + 6, t, SZARY_T, F_MALY);
    int lin = fb_napis_zawin(x + 20, iy + 24, w - 40, wl.nazwa, TEKST, F_TEKST_B, 20);
    int yy = iy + 24 + lin * 20;
    if (wl.uwaga[0]) { ksprintf(t, sizeof t, "(%s)", wl.uwaga); yy += fb_napis_zawin(x + 20, yy, w - 40, t, SZARY_T, F_MALY, 17) * 17; }
    fb_kolo_aa(x + 26, yy + 14, 6, kolor_lit(wl.kolor), 256);
    if (wl.kolor == KOL_BIALY) fb_okrag_aa(x + 26, yy + 14, 6, 1, 0x9A9384, 256);
    ksprintf(t, sizeof t, "%s · %s · rok %c/%s", wl.okres, kolor_nazwa(wl.kolor), wl.cykl_niedz, wl.cykl_powsz == 1 ? "I" : "II");
    fb_napis(x + 40, yy + 6, t, SZARY_T, F_MALY);
}

static void kal_zmien_mies(int o) {
    kal_m += o;
    if (kal_m < 1) { kal_m = 12; kal_r--; }
    if (kal_m > 12) { kal_m = 1; kal_r++; }
    int max = dni_w_miesiacu(kal_r, kal_m);
    if (kal_d > max) kal_d = max;
    okno_brudne(O_KALENDARZ);
}

static void klik_kalendarz(int lx, int ly) {
    int w = okna[O_KALENDARZ].cw, bx = w - 92;
    if (w_prost(lx, ly, bx + 2, 14, 32, 32)) { kal_zmien_mies(-1); return; }
    if (w_prost(lx, ly, bx + 42, 14, 32, 32)) { kal_zmien_mies(1); return; }
    if (ly >= KAL_Y0 && ly < KAL_Y0 + 6 * KAL_KH && lx >= KAL_X0 && lx < KAL_X0 + 7 * KAL_KW) {
        int kol = (lx - KAL_X0) / KAL_KW, wiersz = (ly - KAL_Y0) / KAL_KH;
        int pierwszy = (dzien_tygodnia(dni_od_epoki(kal_r, kal_m, 1)) + 6) % 7;
        int d = wiersz * 7 + kol - pierwszy + 1;
        if (d >= 1 && d <= dni_w_miesiacu(kal_r, kal_m)) { kal_d = d; okno_brudne(O_KALENDARZ); }
    }
}

/* ====================================================================== */
/*  Zegar                                                                  */
/* ====================================================================== */

static void wskazowka(int cx, int cy, int poz, int dl, int ogon, int gr, u32 c) {
    int sx = fb_sinus(poz), sy = fb_sinus(poz + 15);
    fb_linia_aa(cx - sx * ogon / 1000, cy + sy * ogon / 1000, cx + sx * dl / 1000, cy - sy * dl / 1000, gr, c);
}

static void rysuj_zegar(int x, int y) {
    int w = okna[O_ZEGAR].cw;
    czas_t c; rtc_czytaj(&c);
    int cx = x + w / 2, cy = y + 150, r = 118;
    fb_kolo_aa(cx, cy + 4, r + 6, 0x000000, 26);
    fb_kolo_aa(cx, cy, r + 2, 0xFFFFFF, 256);
    fb_okrag_aa(cx, cy, r + 2, 3, 0x2B3550, 256);
    for (int i = 0; i < 60; i++) {
        int dl = (i % 5) ? 6 : 12, gr = (i % 5) ? 1 : 3;
        int x0 = cx + fb_sinus(i) * (r - 10) / 1000, y0 = cy - fb_sinus(i + 15) * (r - 10) / 1000;
        int x1 = cx + fb_sinus(i) * (r - 10 - dl) / 1000, y1 = cy - fb_sinus(i + 15) * (r - 10 - dl) / 1000;
        fb_linia_aa(x0, y0, x1, y1, gr, (i % 5) ? 0xA0A5B0 : 0x2B3550);
    }
    static const char *cyfry[4] = { "12", "3", "6", "9" };
    for (int i = 0; i < 4; i++) {
        int px = cx + fb_sinus(i * 15) * (r - 40) / 1000, py = cy - fb_sinus(i * 15 + 15) * (r - 40) / 1000;
        fb_napis(px - fb_napis_szer(cyfry[i], F_SREDNI) / 2, py - fb_napis_wys(F_SREDNI) / 2, cyfry[i], 0x2B3550, F_SREDNI);
    }
    fb_napis_srodek(cx - 50, 100, cy + 34, "AmenOS", 0xB0A890, F_MALY);
    wskazowka(cx, cy, (c.godz % 12) * 5 + c.min / 12, 58, 10, 6, 0x1E2433);
    wskazowka(cx, cy, c.min, 88, 12, 4, 0x1E2433);
    wskazowka(cx, cy, c.sek, 96, 20, 2, 0xD6333F);
    fb_kolo_aa(cx, cy, 6, 0xD6333F, 256);
    fb_kolo_aa(cx, cy, 2, 0xFFFFFF, 256);
    char t[64];
    ksprintf(t, sizeof t, "%02d:%02d:%02d", c.godz, c.min, c.sek);
    fb_napis_srodek(x, w, y + 290, t, TEKST, F_DUZY);
    ksprintf(t, sizeof t, "%s, %d %s", dni_tyg_wielk[dzien_tygodnia(dni_od_epoki(c.rok, c.mies, c.dzien))], c.dzien, mies_dop[c.mies]);
    fb_napis_srodek(x, w, y + 334, t, SZARY_T, F_TEKST);
}

/* ====================================================================== */
/*  Muzyka mszalna - odtwarzanie w tle                                     */
/* ====================================================================== */

static int muz_wybr, muz_gra = -1, muz_nuta;
static u32 muz_koniec, muz_ton_do, muz_start, muz_calosc, muz_ost_rys;
static int muz_ton;
#define MUZ_WIERSZ 36
#define MUZ_PRZ_Y  (12 + UTWORY_N * MUZ_WIERSZ + 10)

static void muzyka_stop(void) {
    if (muz_gra >= 0) { if (audio_jest) dzw_utwor_stop(); else glosnik_cisza(); }
    muz_gra = -1; muz_ton = 0;
    if (okna[O_MUZYKA].otwarte) okno_brudne(O_MUZYKA);
    brudny_pasek();
}

static void muzyka_nuta(u32 t) {
    const utwor_t *u = &utwory[muz_gra];
    u32 dl = u->czasy[muz_nuta];
    muz_koniec = t + dl;
    if (u->nuty[muz_nuta]) { glosnik_ton(czestotliwosc[u->nuty[muz_nuta]]); muz_ton = 1; muz_ton_do = t + dl * 9 / 10; }
    else { glosnik_cisza(); muz_ton = 0; }
}

static void muzyka_graj(int i) {
    muz_gra = i; muz_nuta = 0; muz_start = zegar_ms();
    muz_calosc = 0;
    for (int k = 0; k < utwory[i].n; k++) muz_calosc += utwory[i].czasy[k];
    if (audio_jest) dzw_utwor(&utwory[i]);           /* karta dźwiękowa: organy, równo co do próbki */
    else muzyka_nuta(muz_start);
    okno_brudne(O_MUZYKA);
    brudny_pasek();
}

static void muzyka_tik(u32 t) {
    if (muz_gra < 0) return;
    if (audio_jest) {                                /* gra sekwencer syntezatora */
        if (!dzw_utwor_gra()) { muzyka_stop(); return; }
        muz_start = t - dzw_utwor_ms();
    } else {                                         /* głośniczek PC: nuta po nucie */
        if (muz_ton && (int)(t - muz_ton_do) >= 0) { glosnik_cisza(); muz_ton = 0; }
        if ((int)(t - muz_koniec) >= 0) {
            if (++muz_nuta >= utwory[muz_gra].n) { muzyka_stop(); return; }
            muzyka_nuta(t);
        }
    }
    if (widoczne(O_MUZYKA) && t - muz_ost_rys > 250) {
        muz_ost_rys = t;
        brudne(okna[O_MUZYKA].x + BR, okna[O_MUZYKA].y + TB + MUZ_PRZ_Y + 44, okna[O_MUZYKA].cw, 50);
    }
}

static void rysuj_muzyke(int x, int y) {
    int w = okna[O_MUZYKA].cw;
    for (int i = 0; i < UTWORY_N; i++) {
        int ry = y + 12 + i * MUZ_WIERSZ;
        if (i == muz_wybr) fb_zaokr(x + 10, ry, w - 20, MUZ_WIERSZ - 4, 8, fb_mieszaj(0xFFFFFF, akcent(), 40), 256);
        int gra = i == muz_gra;
        char n[8]; ksprintf(n, sizeof n, "%d", i + 1);
        if (gra) fb_napis(x + 24, ry + 7, "♫", akcent(), F_TEKST_B);
        else fb_napis(x + 26, ry + 7, n, SZARY_T, F_TEKST);
        fb_napis(x + 52, ry + 7, utwory[i].tytul, gra ? akcent() : TEKST, gra ? F_TEKST_B : F_TEKST);
        int sz = fb_napis_szer(utwory[i].czesc, F_MALY);
        fb_napis(x + w - 24 - sz, ry + 9, utwory[i].czesc, SZARY_T, F_MALY);
    }
    int py = y + MUZ_PRZ_Y;
    przycisk(x + 20, py, 120, 36, "► Graj", 1);
    przycisk(x + 150, py, 120, 36, "■ Stop", 0);
    fb_napis(x + 286, py + 9, "Enter - graj, spacja - stop", SZARY_T, F_MALY);
    int pb = py + 52;
    fb_zaokr(x + 20, pb, w - 40, 6, 3, 0xE2E5EB, 256);
    if (muz_gra >= 0 && muz_calosc) {
        u32 minelo = zegar_ms() - muz_start;
        if (minelo > muz_calosc) minelo = muz_calosc;
        int pel = (int)((w - 40) * minelo / muz_calosc);
        if (pel > 6) fb_zaokr(x + 20, pb, pel, 6, 3, akcent(), 256);
        fb_kolo_aa(x + 20 + pel, pb + 3, 7, 0xFFFFFF, 256);
        fb_okrag_aa(x + 20 + pel, pb + 3, 7, 1, 0x000000, 50);
        char t[96];
        ksprintf(t, sizeof t, "%s   %d:%02d / %d:%02d", utwory[muz_gra].tytul, (int)(minelo / 60000), (int)(minelo / 1000 % 60),
                 (int)(muz_calosc / 60000), (int)(muz_calosc / 1000 % 60));
        fb_napis(x + 20, pb + 16, t, TEKST, F_MALY);
    } else fb_napis(x + 20, pb + 16, "Wybierz utwór i naciśnij Graj. Muzyka gra w tle.", SZARY_T, F_MALY);
}

static void klik_muzyka(int lx, int ly) {
    if (ly >= 12 && ly < 12 + UTWORY_N * MUZ_WIERSZ) {
        int i = (ly - 12) / MUZ_WIERSZ;
        u32 t = zegar_ms();
        if (i == muz_wybr && t - ost_klik_czas < 450) muzyka_graj(i);
        muz_wybr = i; ost_klik_czas = t;
        okno_brudne(O_MUZYKA);
    } else if (w_prost(lx, ly, 20, MUZ_PRZ_Y, 120, 36)) muzyka_graj(muz_wybr);
    else if (w_prost(lx, ly, 150, MUZ_PRZ_Y, 120, 36)) muzyka_stop();
}

/* ====================================================================== */
/*  O systemie, komunikat                                                  */
/* ====================================================================== */

static void rysuj_info(int x, int y) {
    fb_ikona_rys(x + 24, y + 24, 11, 48);
    fb_napis(x + 92, y + 22, "AmenOS " AMENOS_WERSJA, TEKST, F_DUZY);
    fb_napis(x + 92, y + 58, "System operacyjny dla parafii", SZARY_T, F_TEKST);
    char t[96], cpu[52];
    u32 r[12], a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (a >= 0x80000004u) {
        for (u32 i = 0; i < 3; i++)
            __asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3]) : "a"(0x80000002u + i), "c"(0));
        memcpy(cpu, r, 48); cpu[48] = 0;
    } else k_strncpy(cpu, "x86", sizeof cpu);
    char *p = cpu; while (*p == ' ') p++;
    fb_prost(x + 24, y + 92, okna[O_INFO].cw - 48, 1, LINIA);
    const char *et[5] = { "Ekran", "Pamięć", "Procesor", "Sieć", "Dźwięk" };
    char wart[5][64];
    ksprintf(wart[0], 64, "%d × %d", fb_w, fb_h);
    ksprintf(wart[1], 64, "%d MB", (int)(pamiec_kb / 1024));
    fb_napis_przytnij(wart[2], 64, p, okna[O_INFO].cw - 140, F_TEKST);
    if (siec_gotowa) { char ip[16]; ip_tekst(moje_ip, ip); ksprintf(wart[3], 64, "połączona, %s", ip); }
    else k_strncpy(wart[3], "nieuruchomiona", 64);
    fb_napis_przytnij(wart[4], 64, audio_opis, okna[O_INFO].cw - 140, F_TEKST);
    for (int i = 0; i < 5; i++) {
        fb_napis(x + 24, y + 106 + i * 26, et[i], SZARY_T, F_TEKST);
        fb_napis(x + 112, y + 106 + i * 26, wart[i], TEKST, F_TEKST);
    }
    (void)t;
    fb_napis(x + 24, y + 252, "Niech będzie pochwalony Jezus Chrystus", 0x8A6A20, F_TEKST_B);
}

static void rysuj_komunikat(int x, int y) {
    int w = okna[O_KOMUNIKAT].cw;
    fb_ikona_rys(x + 20, y + 20, 11, 32);
    fb_napis_zawin(x + 68, y + 22, w - 92, komunikat_txt, TEKST, F_TEKST, 20);
    przycisk(x + w - 116, y + okna[O_KOMUNIKAT].ch - 52, 96, 34, "OK", 1);
}

/* ====================================================================== */
/*  Ikony, menu Start, uruchamianie                                        */
/* ====================================================================== */

enum { A_TERMINAL, A_KALENDARZ, A_ZEGAR, A_PIESNI, A_MUZYKA, A_WWW, A_MODLITWY, A_SPOWIEDZ, A_KOMUNIA,
       A_OGLOSZENIA, A_MSZE, A_INFO, A_DZIS, A_RESTART, A_WYLACZ, A_ODSTEP, A_WWW_TEKST, A_KALK, A_ZADANIA };

static const struct { const char *nazwa; int ikona, akcja; } ikony[] = {
    { "Terminal", 0, A_TERMINAL }, { "Kalendarz", 1, A_KALENDARZ }, { "Zegar", 2, A_ZEGAR },
    { "Pieśni", 3, A_PIESNI }, { "Muzyka", 4, A_MUZYKA }, { "Internet", 5, A_WWW },
    { "Modlitwy", 6, A_MODLITWY }, { "Spowiedź", 7, A_SPOWIEDZ }, { "Komunia", 8, A_KOMUNIA },
    { "Ogłoszenia", 9, A_OGLOSZENIA }, { "Msze", 10, A_MSZE }, { "Kalkulator", 12, A_KALK },
    { "Zadania", 13, A_ZADANIA }, { "O systemie", 11, A_INFO },
};
#define IKONY_N ((int)(sizeof ikony / sizeof ikony[0]))
#define IK_W 96
#define IK_H 96

static void ikona_poz(int i, int *x, int *y) {
    int na_kol = (fb_h - PASEK_H - 24) / IK_H;
    *x = 14 + (i / na_kol) * IK_W;
    *y = 14 + (i % na_kol) * IK_H;
}

static const struct { const char *nazwa; int ikona, akcja; } menu[] = {
    { "Terminal", 0, A_TERMINAL }, { "Przeglądarka", 5, A_WWW }, { "Kalendarz", 1, A_KALENDARZ },
    { "Dziś", 1, A_DZIS }, { "Zegar", 2, A_ZEGAR }, { "Muzyka", 4, A_MUZYKA },
    { "Pieśni", 3, A_PIESNI }, { "Modlitwy", 6, A_MODLITWY }, { "Spowiedź", 7, A_SPOWIEDZ },
    { "Komunia", 8, A_KOMUNIA }, { "Ogłoszenia", 9, A_OGLOSZENIA }, { "Msze", 10, A_MSZE },
    { "Kalkulator", 12, A_KALK }, { "Zadania", 13, A_ZADANIA }, { "WWW tekst", 0, A_WWW_TEKST },
    { "O systemie", 11, A_INFO },
};
#define MENU_N ((int)(sizeof menu / sizeof menu[0]))
#define MENU_W 652
#define MENU_KOL 6
#define MENU_KW 98
#define MENU_KH 90
#define MENU_SIATKA_Y 64
#define MENU_LIT_H 100
#define MENU_DOL 64
static int menu_wys(void) { return MENU_SIATKA_Y + ((MENU_N + MENU_KOL - 1) / MENU_KOL) * MENU_KH + 16 + MENU_LIT_H + 20 + MENU_DOL; }
static int menu_x(void) { return (fb_w - MENU_W) / 2; }
static int menu_y(void) { return fb_h - PASEK_H - menu_wys() - 12; }
static int menu_lit_y(void) { return MENU_SIATKA_Y + ((MENU_N + MENU_KOL - 1) / MENU_KOL) * MENU_KH + 16; }
static void menu_brudne(void) { brudne(menu_x() - 30, menu_y() - 24, MENU_W + 60, menu_wys() + 60); }

/* pasek zadań: przypięte programy (okna) pośrodku */
static const int PRZYPIETE[] = { O_TERMINAL, O_WWW, O_KALENDARZ, O_ZEGAR, O_MUZYKA, O_KALK };
#define PRZYPIETE_N ((int)(sizeof PRZYPIETE / sizeof PRZYPIETE[0]))
#define PASEK_APPS_N O_N
#define PB 48                                  /* przycisk na pasku */
static int pasek_lista(int *lista) {           /* przypięte programy, potem pozostałe otwarte okna */
    int n = 0;
    for (int i = 0; i < PRZYPIETE_N; i++) lista[n++] = PRZYPIETE[i];
    for (int i = 0; i < O_N; i++) {
        if (i == O_KOMUNIKAT || !okna[i].otwarte) continue;
        int jest = 0;
        for (int k = 0; k < PRZYPIETE_N; k++) if (PRZYPIETE[k] == i) jest = 1;
        if (!jest) lista[n++] = i;
    }
    return n;
}
static int pasek_x0(int n) { return (fb_w - (n + 1) * (PB + 4)) / 2; }
static int lit_w(void) {                        /* obszar obchodu dnia z lewej strony paska */
    int lista[PASEK_APPS_N], w = pasek_x0(pasek_lista(lista)) - 16;
    return w > 370 ? 370 : w;
}

static void wylacz(void);

static int glosnik_x(void) {                       /* środek ikony głośnika w zasobniku */
    char t[24];
    ksprintf(t, sizeof t, "%02d:%02d", dzis.godz, dzis.min);
    int sz = fb_napis_szer(t, F_TEKST);
    ksprintf(t, sizeof t, "%02d.%02d.%d", dzis.dzien, dzis.mies, dzis.rok);
    int sd = fb_napis_szer(t, F_MALY);
    return fb_w - 20 - (sz > sd ? sz : sd) - 40;
}

static void uruchom(int a) {
    switch (a) {
    case A_TERMINAL:   okno_otworz(O_TERMINAL); break;
    case A_KALENDARZ:  kal_r = dzis.rok; kal_m = dzis.mies; kal_d = dzis.dzien; okno_otworz(O_KALENDARZ); break;
    case A_ZEGAR:      okno_otworz(O_ZEGAR); break;
    case A_MUZYKA:     okno_otworz(O_MUZYKA); break;
    case A_INFO:       okno_otworz(O_INFO); break;
    case A_DZIS:       uruchom(A_KALENDARZ); break;
    case A_PIESNI:     okno_otworz(O_APKA + AP_PIESNI); break;
    case A_WWW:
        okno_otworz(O_WWW);
        if (!www_start) { www_start = 1; wwwg_otworz("about:start"); }
        break;
    case A_WWW_TEKST:  polecenie("www"); break;
    case A_KALK:       okno_otworz(O_KALK); break;
    case A_MODLITWY:   okno_otworz(O_APKA + AP_MODLITWY); break;
    case A_SPOWIEDZ:   okno_otworz(O_APKA + AP_SPOWIEDZ); break;
    case A_KOMUNIA:    okno_otworz(O_APKA + AP_KOMUNIA); break;
    case A_OGLOSZENIA: okno_otworz(O_APKA + AP_OGLOSZENIA); break;
    case A_MSZE:       okno_otworz(O_APKA + AP_MSZE); break;
    case A_ZADANIA:    okno_otworz(O_APKA + AP_ZADANIA); break;
    case A_RESTART:    while (inb(0x64) & 2) ; outb(0x64, 0xFE); break;
    case A_WYLACZ:     wylacz(); break;
    }
}

/* ====================================================================== */
/*  Rysowanie całości                                                      */
/* ====================================================================== */

static void napis_cien(int x, int y, const char *s, u32 c, int f) {
    fb_napis(x, y + 1, s, 0x060914, f);
    fb_napis(x, y, s, c, f);
}

#define WID_W 340
static int wid_h(void) {
    int lin = fb_napis_zawin(0, 0, WID_W - 64, dzis_lit.nazwa, FB_PRZEZR, F_TEKST_B, 19);
    if (lin > 2) lin = 2;
    return 134 + lin * 19 + 36;
}
static void widzet_brudny(void) { brudne(fb_w - WID_W - 40, 8, WID_W + 40, wid_h() + 40); }

static void rysuj_widzet(void) {
    int w = WID_W, h = wid_h(), x = fb_w - w - 24, y = 24;
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    if (x > kx + kw || y > ky + kh || x + w < kx || y + h < ky) return;
    fb_zaokr(x, y, w, h, 16, 0x0B1029, 120);
    fb_zaokr_ramka(x, y, w, h, 16, 0xFFFFFF, 34);
    char t[120];
    ksprintf(t, sizeof t, "%02d:%02d", dzis.godz, dzis.min);
    fb_napis(x + 22, y + 6, t, 0xFFFFFF, F_OGROMNY);
    ksprintf(t, sizeof t, "%s, %d %s", dni_tyg_wielk[dzien_tygodnia(dni_od_epoki(dzis.rok, dzis.mies, dzis.dzien))],
             dzis.dzien, mies_dop[dzis.mies]);
    fb_napis(x + 24, y + 94, t, 0xD8DEEE, F_SREDNI);
    fb_kolo_aa(x + 30, y + 139, 6, kolor_lit(dzis_lit.kolor), 256);
    fb_okrag_aa(x + 30, y + 139, 7, 1, 0xFFFFFF, 160);
    int lin = fb_napis_zawin(x + 44, y + 130, w - 64, dzis_lit.nazwa, 0xF6DE8E, F_TEKST_B, 19);
    if (lin > 2) lin = 2;
    ksprintf(t, sizeof t, "%s · %s", dzis_lit.okres, kolor_nazwa(dzis_lit.kolor));
    fb_napis(x + 44, y + 134 + lin * 19, t, 0xAAB3CC, F_MALY);
}

static void rysuj_pulpit(void) {
    fb_tapeta(0, 0, fb_w, fb_h);                 /* także pod półprzezroczystym paskiem zadań */
    rysuj_widzet();
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    for (int i = 0; i < IKONY_N; i++) {
        int x, y; ikona_poz(i, &x, &y);
        if (x > kx + kw || y > ky + kh || x + IK_W < kx || y + IK_H < ky) continue;
        if (i == wybrana_ikona) { fb_zaokr(x, y, IK_W - 6, IK_H - 6, 10, 0xFFFFFF, 52); fb_zaokr_ramka(x, y, IK_W - 6, IK_H - 6, 10, 0xFFFFFF, 70); }
        else if (hov_typ == H_IKONA && hov_id == i) fb_zaokr(x, y, IK_W - 6, IK_H - 6, 10, 0xFFFFFF, 28);
        fb_ikona_rys(x + (IK_W - 6 - 48) / 2, y + 8, ikony[i].ikona, 48);
        int sz = fb_napis_szer(ikony[i].nazwa, F_TEKST);
        napis_cien(x + (IK_W - 6 - sz) / 2, y + 62, ikony[i].nazwa, 0xFFFFFF, F_TEKST);
    }
}

static void rysuj_okno(int i) {
    okno_t *o = &okna[i];
    int x = o->x, y = o->y, w = ow(i), h = oh(i);
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    if (x - 26 > kx + kw || y - 18 > ky + kh || x + w + 26 < kx || y + h + 34 < ky) return;
    int akt = (i == okno_aktywne());
    int ciemne = i == O_TERMINAL;
    fb_cien(x, y, w, h, R_OKNA, 24, akt ? 120 : 70, 8);
    fb_rogi_zapisz(x, y, w, h, R_OKNA);
    u32 belka = ciemne ? (akt ? TERM_BELKA : 0x2A2C35) : (akt ? BELKA : BELKA_NA);
    fb_prost(x, y, w, TB, belka);
    fb_ikona_rys(x + 12, y + 8, o->ikona, 24);
    if (i == O_WWW) o->tytul = wwwg_tytul();
    char t[160];
    fb_napis_przytnij(t, sizeof t, o->tytul, w - 2 * PRZ_B - 60, F_TEKST);
    u32 kt = ciemne ? (akt ? 0xF2F4F8 : 0x9A9EA8) : (akt ? TEKST : 0x8A8F9C);
    fb_napis(x + 46, y + (TB - fb_napis_wys(F_TEKST)) / 2, t, kt, F_TEKST);
    /* przyciski: minimalizuj i zamknij */
    int bz = x + w - PRZ_B;
    int hz = hov_typ == H_ZAMKNIJ && hov_id == i, hm = hov_typ == H_MINIM && hov_id == i;
    if (hz) fb_prost(bz, y, PRZ_B, TB, 0xE5353F);
    u32 ki = hz ? 0xFFFFFF : kt;
    fb_linia_aa(bz + 18, y + 15, bz + 28, y + 25, 1, ki);
    fb_linia_aa(bz + 28, y + 15, bz + 18, y + 25, 1, ki);
    if (i != O_KOMUNIKAT) {
        if (hm) fb_prost(bz - PRZ_B, y, PRZ_B, TB, ciemne ? 0x343743 : 0xDFE3EA);
        fb_linia_aa(bz - PRZ_B + 18, y + 20, bz - PRZ_B + 28, y + 20, 1, kt);
    }
    int cx = x + BR, cy = y + TB;
    fb_prost(x, cy, w, h - TB, ciemne ? TERM_TLO : TLO_OKNA);
    if (!ciemne) fb_prost(x, cy, w, 1, LINIA);
    fb_klip_zawez(cx, cy, o->cw, o->ch);
    switch (i) {
    case O_TERMINAL:  rysuj_terminal(cx, cy); break;
    case O_KALENDARZ: rysuj_kalendarz(cx, cy); break;
    case O_ZEGAR:     rysuj_zegar(cx, cy); break;
    case O_MUZYKA:    rysuj_muzyke(cx, cy); break;
    case O_INFO:      rysuj_info(cx, cy); break;
    case O_KOMUNIKAT: rysuj_komunikat(cx, cy); break;
    case O_WWW:       wwwg_rysuj(cx, cy); break;
    case O_KALK:      kalk_rysuj(cx, cy); break;
    default:          if (JEST_APKA(i)) apki[i - O_APKA].rysuj(cx, cy); break;
    }
    fb_klip(kx, ky, kw, kh);
    fb_rogi_przywroc(x, y, w, h, R_OKNA, FB_PRZEZR);
    fb_zaokr_ramka(x, y, w, h, R_OKNA, ciemne ? 0xFFFFFF : 0x000000, ciemne ? 30 : (akt ? 60 : 40));
}

static void rysuj_pasek(void) {
    int y = fb_h - PASEK_H;
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    if (ky + kh <= y) return;
    fb_prost_alfa(0, y, fb_w, PASEK_H, 0x0E1124, 205);
    fb_prost_alfa(0, y, fb_w, 1, 0xFFFFFF, 30);
    /* lewa strona: obchód dnia */
    char t[160];
    int hl = hov_typ == H_LIT && hov_id == 0;
    int lw = lit_w();
    if (hl) fb_zaokr(8, y + 6, lw - 8, PASEK_H - 12, 8, 0xFFFFFF, 22);
    fb_kolo_aa(28, y + PASEK_H / 2, 8, kolor_lit(dzis_lit.kolor), 256);
    fb_okrag_aa(28, y + PASEK_H / 2, 9, 1, 0xFFFFFF, 140);
    fb_napis_przytnij(t, sizeof t, dzis_lit.nazwa, lw - 56, F_TEKST_B);
    fb_napis(46, y + 8, t, 0xFFFFFF, F_TEKST_B);
    fb_napis_przytnij(t, sizeof t, dzis_lit.okres, lw - 56, F_MALY);
    fb_napis(46, y + 27, t, 0xA8B0C8, F_MALY);
    /* środek: Start i programy */
    int lista[PASEK_APPS_N], n = pasek_lista(lista), bx = pasek_x0(n), akt = okno_aktywne();
    for (int k = 0; k <= n; k++) {
        int px = bx + k * (PB + 4), py = y + 2;
        int h = hov_typ == H_PASEK && hov_id == k;
        int okno = k ? lista[k - 1] : -1;
        int aktywny = k == 0 ? menu_otwarte : (okno == akt && !menu_otwarte);
        if (aktywny) fb_zaokr(px, py, PB, PB, 8, 0xFFFFFF, 40);
        else if (h) fb_zaokr(px, py, PB, PB, 8, 0xFFFFFF, 24);
        fb_ikona_rys(px + (PB - 32) / 2, py + (PB - 32) / 2 - 2, k ? okna[okno].ikona : 11, 32);
        if (k && okna[okno].otwarte) {
            int sz = aktywny ? 16 : 6;
            fb_zaokr(px + (PB - sz) / 2, y + PASEK_H - 6, sz, 3, 1, aktywny ? 0x8CC4FF : 0xB8BECC, 256);
        }
    }
    /* zasobnik: muzyka, godzina, data */
    ksprintf(t, sizeof t, "%02d:%02d", dzis.godz, dzis.min);
    int sz = fb_napis_szer(t, F_TEKST);
    fb_napis(fb_w - 20 - sz, y + 8, t, 0xFFFFFF, F_TEKST);
    ksprintf(t, sizeof t, "%02d.%02d.%d", dzis.dzien, dzis.mies, dzis.rok);
    int sd = fb_napis_szer(t, F_MALY);
    fb_napis(fb_w - 20 - sd, y + 28, t, 0xC8CEDC, F_MALY);
    int lewo = fb_w - 20 - (sz > sd ? sz : sd);
    if (audio_jest) {                                /* głośnik: kliknięcie zmienia głośność */
        int gx = lewo - 40, gy = y + PASEK_H / 2, g = audio_glosnosc_pobierz();
        if (hov_typ == H_GLOSNIK) fb_zaokr(gx - 10, y + 8, 40, PASEK_H - 16, 8, 0xFFFFFF, 24);
        fb_prost(gx - 2, gy - 4, 5, 8, 0xFFFFFF);
        for (int k = 0; k < 7; k++) fb_prost(gx + 3, gy - 4 - k, 1 + k / 2 + 1, 8 + 2 * k, 0xFFFFFF);
        int kx, ky, kw, kh;
        fb_klip_pobierz(&kx, &ky, &kw, &kh);
        fb_klip_zawez(gx + 12, gy - 12, 16, 24);
        if (g > 0) fb_okrag_aa(gx + 9, gy, 6, 2, 0xFFFFFF, 256);
        if (g > 40) fb_okrag_aa(gx + 9, gy, 10, 2, 0xFFFFFF, 256);
        if (g > 75) fb_okrag_aa(gx + 9, gy, 14, 2, 0xFFFFFF, 256);
        fb_klip(kx, ky, kw, kh);
        if (!g) { fb_linia_aa(gx + 13, gy - 5, gx + 22, gy + 5, 2, 0xFF8A8A); fb_linia_aa(gx + 22, gy - 5, gx + 13, gy + 5, 2, 0xFF8A8A); }
        lewo -= 44;
    }
    if (muz_gra >= 0) fb_napis(lewo - 30, y + 15, "♫", 0xF6DE8E, F_SREDNI);
}

static void rysuj_menu(void) {
    int x = menu_x(), y = menu_y(), w = MENU_W, h = menu_wys();
    fb_cien(x, y, w, h, 14, 28, 130, 10);
    fb_rogi_zapisz(x, y, w, h, 14);
    fb_prost(x, y, w, h, 0xF4F5F8);
    fb_prost(x, y + h - MENU_DOL, w, MENU_DOL, 0xE9EBF0);
    fb_prost(x, y + h - MENU_DOL, w, 1, 0xDCDFE6);
    fb_napis(x + 34, y + 26, "Przypięte", TEKST, F_TEKST_B);
    fb_napis(x + w - 34 - fb_napis_szer("AmenOS " AMENOS_WERSJA, F_MALY), y + 28, "AmenOS " AMENOS_WERSJA, SZARY_T, F_MALY);
    for (int i = 0; i < MENU_N; i++) {
        int cx = x + 32 + (i % MENU_KOL) * MENU_KW, cy = y + MENU_SIATKA_Y + (i / MENU_KOL) * MENU_KH;
        if (hov_typ == H_MENU && hov_id == i) fb_zaokr(cx, cy, MENU_KW - 4, MENU_KH - 6, 8, 0xFFFFFF, 256);
        fb_ikona_rys(cx + (MENU_KW - 4 - 32) / 2, cy + 12, menu[i].ikona, 32);
        char t[40]; fb_napis_przytnij(t, sizeof t, menu[i].nazwa, MENU_KW - 10, F_MALY);
        fb_napis_srodek(cx, MENU_KW - 4, cy + 54, t, TEKST, F_MALY);
    }
    /* karta "Dziś w liturgii" */
    int ly = y + menu_lit_y();
    int hl = hov_typ == H_MENU && hov_id == 100;
    fb_zaokr(x + 32, ly, w - 64, MENU_LIT_H, 10, hl ? 0xFFFFFF : 0xFBFBFD, 256);
    fb_zaokr_ramka(x + 32, ly, w - 64, MENU_LIT_H, 10, 0x000000, 26);
    fb_zaokr(x + 32, ly, 6, MENU_LIT_H, 3, kolor_lit(dzis_lit.kolor), 256);
    char t[120];
    ksprintf(t, sizeof t, "Dziś w liturgii · %s, %d %s %d", dni_tyg_wielk[dzien_tygodnia(dni_od_epoki(dzis.rok, dzis.mies, dzis.dzien))],
             dzis.dzien, mies_dop[dzis.mies], dzis.rok);
    fb_napis(x + 54, ly + 14, t, SZARY_T, F_MALY);
    int lin = fb_napis_zawin(x + 54, ly + 34, w - 120, dzis_lit.nazwa, TEKST, F_SREDNI, 24);
    if (lin > 2) lin = 2;
    ksprintf(t, sizeof t, "%s · szaty: %s", dzis_lit.okres, kolor_nazwa(dzis_lit.kolor));
    if (lin < 2) fb_napis(x + 54, ly + 38 + lin * 24, t, SZARY_T, F_MALY);
    szewron(x + w - 54, ly + MENU_LIT_H / 2, -1, SZARY_T);
    /* dół: zasilanie */
    int dy = y + h - MENU_DOL;
    fb_ikona_rys(x + 30, dy + 16, 11, 32);
    fb_napis(x + 72, dy + 14, "Parafia", TEKST, F_TEKST_B);
    fb_napis(x + 72, dy + 32, "Niech będzie pochwalony Jezus Chrystus", SZARY_T, F_MALY);
    const char *pt[2] = { "Uruchom ponownie", "Wyłącz" };
    int px = x + w - 24;
    for (int k = 1; k >= 0; k--) {
        int bw = fb_napis_szer(pt[k], F_TEKST) + (k ? 52 : 28);
        px -= bw;
        if (hov_typ == H_MENU_DOL && hov_id == k) fb_zaokr(px, dy + 12, bw, 40, 8, k ? 0xF6D6D8 : 0xDDE1E8, 256);
        int tx = px + 14;
        if (k) {                                          /* symbol zasilania */
            int cx = px + 22, cy = dy + 32;
            fb_okrag_aa(cx, cy, 8, 2, 0xB42A32, 256);
            fb_prost(cx - 3, cy - 10, 6, 7, hov_typ == H_MENU_DOL && hov_id == k ? 0xF6D6D8 : 0xE9EBF0);
            fb_linia_aa(cx, cy - 10, cx, cy - 2, 2, 0xB42A32);
            tx = px + 38;
        }
        fb_napis(tx, dy + 23, pt[k], k ? 0xB42A32 : TEKST, F_TEKST);
        px -= 8;
    }
    fb_rogi_przywroc(x, y, w, h, 14, FB_PRZEZR);
    fb_zaokr_ramka(x, y, w, h, 14, 0x000000, 50);
}

static void komponuj(void) {
    int x = bx0 < 0 ? 0 : bx0, y = by0 < 0 ? 0 : by0;
    int w = (bx1 > fb_w ? fb_w : bx1) - x, h = (by1 > fb_h ? fb_h : by1) - y;
    brud = 0;
    if (w <= 0 || h <= 0) return;
    fb_klip(x, y, w, h);
    rysuj_pulpit();
    for (int k = 0; k < zn; k++) if (widoczne(z[k])) rysuj_okno(z[k]);
    rysuj_pasek();
    if (menu_otwarte) rysuj_menu();
    fb_kursor(mx, my);
    fb_klip_caly();
    fb_wyslij(x, y, w, h);
}

/* ---------- ekran końcowy ---------- */

static void ekran_napis(const char *duzy, const char *maly) {
    fb_klip_caly();
    fb_tapeta(0, 0, fb_w, fb_h);
    fb_prost_alfa(0, 0, fb_w, fb_h, 0x05081A, 120);
    fb_napis_srodek(0, fb_w, fb_h / 2 - 70, duzy, 0xF6DE8E, F_OGROMNY);
    if (maly) fb_napis_srodek(0, fb_w, fb_h / 2 + 34, maly, 0xFFFFFF, F_SREDNI);
}

static void wylacz(void) {
    glosnik_cisza();
    outw(0x604, 0x2000); outw(0xB004, 0x2000); outw(0x4004, 0x3400);
    ekran_napis("Szczęść Boże!", "Można bezpiecznie wyłączyć komputer.");
    fb_wyslij(0, 0, fb_w, fb_h);
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* ====================================================================== */
/*  Mysz i klawiatura                                                      */
/* ====================================================================== */

static void klik_pasek(int x) {
    int lista[PASEK_APPS_N], n = pasek_lista(lista), bx = pasek_x0(n);
    if (x < lit_w()) { uruchom(A_KALENDARZ); return; }
    int gx = glosnik_x();
    if (audio_jest && x >= gx - 10 && x < gx + 30) {
        int g = audio_glosnosc_pobierz();
        audio_glosnosc(g > 80 ? 60 : g > 45 ? 30 : g > 0 ? 0 : 100);
        brudny_pasek();
        dzw_efekt(E_POWIADOM);
        return;
    }
    for (int k = 0; k <= n; k++) {
        int px = bx + k * (PB + 4);
        if (x < px || x >= px + PB) continue;
        if (k == 0) { menu_otwarte = !menu_otwarte; menu_brudne(); brudny_pasek(); return; }
        int i = lista[k - 1];
        if (!okna[i].otwarte) {
            static const int akcja[O_APKA] = { A_TERMINAL, A_KALENDARZ, A_ZEGAR, A_MUZYKA, A_INFO, A_WWW, A_KALK };
            if (i < O_APKA) uruchom(akcja[i]); else okno_otworz(i);
        } else if (okno_aktywne() == i) okno_minimalizuj(i);
        else { okna[i].zminim = 0; okno_podnies(i); }
        return;
    }
}

static void klik(int x, int y) {
    u32 t = zegar_ms();
    if (menu_otwarte) {
        int mx0 = menu_x(), my0 = menu_y(), h = menu_wys();
        menu_otwarte = 0;
        menu_brudne();
        brudny_pasek();
        if (w_prost(x, y, mx0, my0, MENU_W, h)) {
            int lx = x - mx0, ly = y - my0;
            for (int i = 0; i < MENU_N; i++)
                if (w_prost(lx, ly, 32 + (i % MENU_KOL) * MENU_KW, MENU_SIATKA_Y + (i / MENU_KOL) * MENU_KH, MENU_KW - 4, MENU_KH - 6)) {
                    uruchom(menu[i].akcja); return;
                }
            if (w_prost(lx, ly, 32, menu_lit_y(), MENU_W - 64, MENU_LIT_H)) { uruchom(A_KALENDARZ); return; }
            if (hov_typ == H_MENU_DOL) { uruchom(hov_id ? A_WYLACZ : A_RESTART); return; }
            return;
        }
        if (y >= fb_h - PASEK_H) {
            int lista[PASEK_APPS_N], n = pasek_lista(lista), bx = pasek_x0(n);
            if (x >= bx && x < bx + PB) return;               /* ponowne kliknięcie Start zamyka menu */
            (void)n;
        }
    }
    if (y >= fb_h - PASEK_H) { klik_pasek(x); return; }
    for (int k = zn - 1; k >= 0; k--) {
        int i = z[k];
        if (!widoczne(i)) continue;
        okno_t *o = &okna[i];
        if (!w_prost(x, y, o->x, o->y, ow(i), oh(i))) continue;
        okno_podnies(i);
        if (y < o->y + TB) {
            int bz = o->x + ow(i) - PRZ_B;
            if (x >= bz) okno_zamknij(i);
            else if (i != O_KOMUNIKAT && x >= bz - PRZ_B) okno_minimalizuj(i);
            else { ciagniete = i; cdx = x - o->x; cdy = y - o->y; }
        } else if (x >= o->x + BR) {
            int lx = x - o->x - BR, ly = y - o->y - TB;
            if (i == O_KALENDARZ) klik_kalendarz(lx, ly);
            else if (i == O_MUZYKA) klik_muzyka(lx, ly);
            else if (i == O_WWW) { klient_ciag = O_WWW; wwwg_klik(lx, ly); }
            else if (i == O_KALK) { klient_ciag = O_KALK; kalk_klik(lx, ly); }
            else if (JEST_APKA(i)) apki[i - O_APKA].klik(lx, ly);
            else if (i == O_KOMUNIKAT && w_prost(lx, ly, o->cw - 116, o->ch - 52, 96, 34)) okno_zamknij(i);
        }
        return;
    }
    for (int i = 0; i < IKONY_N; i++) {
        int ix, iy; ikona_poz(i, &ix, &iy);
        if (!w_prost(x, y, ix, iy, IK_W - 6, IK_H - 6)) continue;
        int stara = wybrana_ikona;
        if (stara >= 0) { int sx, sy; ikona_poz(stara, &sx, &sy); brudne(sx, sy, IK_W, IK_H); }
        brudne(ix, iy, IK_W, IK_H);
        wybrana_ikona = i;
        if (stara == i && t - ost_klik_czas < 500) { ost_klik_czas = 0; uruchom(ikony[i].akcja); }
        else ost_klik_czas = t;
        return;
    }
    if (wybrana_ikona >= 0) { int sx, sy; ikona_poz(wybrana_ikona, &sx, &sy); brudne(sx, sy, IK_W, IK_H); wybrana_ikona = -1; }
}

/* co jest pod myszą - do podświetleń */
static void ustal_najechanie(void) {
    int typ = H_BRAK, id = 0, rx = 0, ry = 0, rw = 0, rh = 0;
    if (ciagniete >= 0) goto koniec;
    if (menu_otwarte) {
        int x0 = menu_x(), y0 = menu_y(), lx = mx - x0, ly = my - y0, h = menu_wys();
        if (w_prost(mx, my, x0, y0, MENU_W, h)) {
            for (int i = 0; i < MENU_N; i++) {
                int cx = 32 + (i % MENU_KOL) * MENU_KW, cy = MENU_SIATKA_Y + (i / MENU_KOL) * MENU_KH;
                if (w_prost(lx, ly, cx, cy, MENU_KW - 4, MENU_KH - 6)) { typ = H_MENU; id = i; rx = x0 + cx; ry = y0 + cy; rw = MENU_KW; rh = MENU_KH; goto koniec; }
            }
            if (w_prost(lx, ly, 32, menu_lit_y(), MENU_W - 64, MENU_LIT_H)) { typ = H_MENU; id = 100; rx = x0; ry = y0 + menu_lit_y(); rw = MENU_W; rh = MENU_LIT_H; goto koniec; }
            if (ly >= h - MENU_DOL + 12 && ly < h - 12) {
                const char *pt[2] = { "Uruchom ponownie", "Wyłącz" };
                int px = MENU_W - 24;
                for (int k = 1; k >= 0; k--) {
                    int bw = fb_napis_szer(pt[k], F_TEKST) + (k ? 52 : 28);
                    px -= bw;
                    if (lx >= px && lx < px + bw) { typ = H_MENU_DOL; id = k; rx = x0; ry = y0 + h - MENU_DOL; rw = MENU_W; rh = MENU_DOL; goto koniec; }
                    px -= 8;
                }
            }
            goto koniec;
        }
    }
    if (my >= fb_h - PASEK_H) {
        int lista[PASEK_APPS_N], n = pasek_lista(lista), bx = pasek_x0(n);
        if (mx < lit_w()) { typ = H_LIT; id = 0; rx = 0; ry = fb_h - PASEK_H; rw = lit_w() + 4; rh = PASEK_H; goto koniec; }
        int gx = glosnik_x();
        if (audio_jest && mx >= gx - 10 && mx < gx + 30) { typ = H_GLOSNIK; rx = gx - 12; ry = fb_h - PASEK_H; rw = 46; rh = PASEK_H; goto koniec; }
        for (int k = 0; k <= n; k++) {
            int px = bx + k * (PB + 4);
            if (mx >= px && mx < px + PB) { typ = H_PASEK; id = k; rx = px; ry = fb_h - PASEK_H; rw = PB; rh = PASEK_H; goto koniec; }
        }
        goto koniec;
    }
    for (int k = zn - 1; k >= 0; k--) {
        int i = z[k];
        if (!widoczne(i)) continue;
        okno_t *o = &okna[i];
        if (!w_prost(mx, my, o->x, o->y, ow(i), oh(i))) continue;
        if (my < o->y + TB) {
            int bz = o->x + ow(i) - PRZ_B;
            if (mx >= bz) { typ = H_ZAMKNIJ; id = i; }
            else if (i != O_KOMUNIKAT && mx >= bz - PRZ_B) { typ = H_MINIM; id = i; }
            if (typ != H_BRAK) { rx = bz - PRZ_B; ry = o->y; rw = 2 * PRZ_B; rh = TB; }
        } else if (i == O_KALENDARZ) {
            int lx = mx - o->x - BR, ly = my - o->y - TB, bx = o->cw - 92;
            if (w_prost(lx, ly, bx + 2, 14, 32, 32)) id = 0, typ = H_LIT;
            else if (w_prost(lx, ly, bx + 42, 14, 32, 32)) id = 1, typ = H_LIT;
            if (typ != H_BRAK) { typ = H_LIT; id += 10; rx = o->x + BR + bx; ry = o->y + TB + 10; rw = 80; rh = 40; }
        }
        goto koniec;
    }
    for (int i = 0; i < IKONY_N; i++) {
        int ix, iy; ikona_poz(i, &ix, &iy);
        if (w_prost(mx, my, ix, iy, IK_W - 6, IK_H - 6)) { typ = H_IKONA; id = i; rx = ix; ry = iy; rw = IK_W; rh = IK_H; break; }
    }
koniec:
    if (typ != hov_typ || id != hov_id) {
        if (hov_typ != H_BRAK) brudne(hov_x, hov_y, hov_w, hov_h);
        if (typ != H_BRAK) brudne(rx, ry, rw, rh);
        hov_typ = typ; hov_id = id; hov_x = rx; hov_y = ry; hov_w = rw; hov_h = rh;
    }
}

void gui_mysz(int dx, int dy, int przyciski) {
    if (dx > 6 || dx < -6) dx *= 2;
    if (dy > 6 || dy < -6) dy *= 2;
    brudne(mx - 2, my - 2, KURSOR_SZ + 2, KURSOR_WYS + 2);
    mx += dx; my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > fb_w - 1) mx = fb_w - 1;
    if (my > fb_h - 1) my = fb_h - 1;
    brudne(mx - 2, my - 2, KURSOR_SZ + 2, KURSOR_WYS + 2);

    if (ciagniete >= 0 && (przyciski & 1) && (dx || dy)) {
        okno_t *o = &okna[ciagniete];
        okno_brudne(ciagniete);
        o->x = mx - cdx; o->y = my - cdy;
        if (o->x < 80 - ow(ciagniete)) o->x = 80 - ow(ciagniete);
        if (o->x > fb_w - 80) o->x = fb_w - 80;
        if (o->y < 0) o->y = 0;
        if (o->y > fb_h - PASEK_H - TB) o->y = fb_h - PASEK_H - TB;
        okno_brudne(ciagniete);
    }
    if ((przyciski & 1) && klient_ciag == O_WWW && (dx || dy) && widoczne(O_WWW))
        wwwg_ciagnij(mx - okna[O_WWW].x - BR, my - okna[O_WWW].y - TB);
    if ((przyciski & 1) && !(przyc & 1)) klik(mx, my);
    if (!(przyciski & 1)) {
        ciagniete = -1;
        if (klient_ciag == O_WWW) wwwg_puszczenie();
        if (klient_ciag == O_KALK) kalk_puszczenie();
        klient_ciag = -1;
        /* link pod myszą w przeglądarce */
        int nad = -1;
        for (int k = zn - 1; k >= 0; k--) {
            int i = z[k];
            if (!widoczne(i)) continue;
            if (w_prost(mx, my, okna[i].x, okna[i].y, ow(i), oh(i))) { nad = i; break; }
        }
        if (nad == O_WWW && !menu_otwarte) { wwwg_najedz(mx - okna[O_WWW].x - BR, my - okna[O_WWW].y - TB); www_pod = 1; }
        else if (www_pod) { wwwg_najedz(-1, -1); www_pod = 0; }
    }
    ustal_najechanie();
    przyc = przyciski;
}

void gui_klawisz(int k) {
    if (menu_otwarte) {
        if (k == KEY_ESC) { menu_otwarte = 0; menu_brudne(); brudny_pasek(); }
        return;
    }
    int a = okno_aktywne();
    if (a < 0) return;
    if (a == O_WWW) { wwwg_klawisz(k); return; }
    if (a == O_KALK && k != KEY_ESC) { kalk_klawisz(k); return; }
    if (JEST_APKA(a)) { if (!apki[a - O_APKA].klawisz(k) && k == KEY_ESC) okno_zamknij(a); return; }
    if (k == KEY_ESC) { okno_zamknij(a); return; }
    if (a == O_KALENDARZ) {
        if (k == KEY_LEFT || k == KEY_PGUP) kal_zmien_mies(-1);
        else if (k == KEY_RIGHT || k == KEY_PGDN) kal_zmien_mies(1);
    } else if (a == O_MUZYKA) {
        if (k == KEY_UP && muz_wybr > 0) { muz_wybr--; okno_brudne(a); }
        else if (k == KEY_DOWN && muz_wybr < UTWORY_N - 1) { muz_wybr++; okno_brudne(a); }
        else if (k == KEY_ENTER) muzyka_graj(muz_wybr);
        else if (k == ' ') muzyka_stop();
    } else if (a == O_KOMUNIKAT && k == KEY_ENTER) okno_zamknij(a);
}

/* ====================================================================== */
/*  Usługi pulpitu dla programów (aplikacje.c)                             */
/* ====================================================================== */

void gui_apka_brudne(int a, int x, int y, int w, int h) {
    int i = O_APKA + a;
    if (widoczne(i)) brudne(okna[i].x + BR + x, okna[i].y + TB + y, w, h);
}
void gui_apka_cala(int a) { gui_apka_brudne(a, 0, 0, okna[O_APKA + a].cw, okna[O_APKA + a].ch); }
int  gui_apka_otwarta(int a) { return okna[O_APKA + a].otwarte; }
void gui_apka_zamknij(int a) { if (okna[O_APKA + a].otwarte) okno_zamknij(O_APKA + a); }
u32  gui_akcent(void) { return akcent(); }
u32  gui_kolor_lit(int k) { return kolor_lit(k); }
void gui_muzyka_graj(int i) { muzyka_graj(i); }
void gui_muzyka_stop(void) { if (muz_gra >= 0) muzyka_stop(); }
void gui_komunikat(const char *s) { komunikat(s); }
int  gui_okna_lista(int *id, int max) {
    int n = 0;
    for (int i = 0; i < O_N && n < max; i++) if (okna[i].otwarte && i != O_KOMUNIKAT) id[n++] = i;
    return n;
}
const char *gui_okno_tytul(int id) { return id == O_WWW ? wwwg_tytul() : okna[id].tytul; }
int  gui_okno_ikona(int id) { return okna[id].ikona; }
int  gui_okno_stan(int id) { return okna[id].zminim ? 2 : (id == okno_aktywne() ? 0 : 1); }
int  gui_okno_pamiec_kb(int id) { return okna[id].cw * okna[id].ch * 4 / 1024 + 24; }
void gui_okno_przelacz(int id) { if (okna[id].otwarte) { okna[id].zminim = 0; okno_podnies(id); } }
void gui_okno_zamknij_id(int id) { if (id >= 0 && id < O_N && okna[id].otwarte) okno_zamknij(id); }

/* ====================================================================== */
/*  Pompa zdarzeń                                                          */
/* ====================================================================== */

void gui_pompuj(void) {
    static int w_pompie;
    static u32 ost_rys, ost_spr;
    static int ost_sek = -1, ost_min = -1;
    if (w_pompie || !gui_aktywne) return;
    w_pompie = 1;
    ps2_obsluz();
    u32 t = zegar_ms();
    muzyka_tik(t);
    if (t - ost_spr >= 200) {
        ost_spr = t;
        rtc_czytaj(&dzis);
        if (dzis.sek != ost_sek) {
            ost_sek = dzis.sek;
            if (widoczne(O_ZEGAR)) okno_brudne(O_ZEGAR);
            if (dzis.min != ost_min) {
                /* pełna godzina przy otwartym zegarze: dzwon wybija godziny jak na wieży kościoła */
                if (ost_min >= 0 && dzis.min == 0 && widoczne(O_ZEGAR) && audio_jest)
                    dzw_bij(dzis.godz % 12 ? dzis.godz % 12 : 12, 196000, 1900);
                ost_min = dzis.min;
                liturgia(dzis.rok, dzis.mies, dzis.dzien, &dzis_lit);
                brudny_pasek();
                widzet_brudny();
            }
        }
    }
    terminal_sprawdz(t);
    for (int a = 0; a < AP_N; a++) if (okna[O_APKA + a].otwarte) apki[a].tik(t);   /* wszystkie programy naraz */
    if (brud && t - ost_rys >= 12) { komponuj(); ost_rys = t; }
    if (okna[O_WWW].otwarte) wwwg_tik();
    w_pompie = 0;
}

/* ====================================================================== */
/*  Start                                                                  */
/* ====================================================================== */

void gui_init(u32 pam) {
    pamiec_kb = pam;
    rtc_czytaj(&dzis);
    liturgia(dzis.rok, dzis.mies, dzis.dzien, &dzis_lit);
    kal_r = dzis.rok; kal_m = dzis.mies; kal_d = dzis.dzien;
    fb_tapeta_init();
    int obszar_h = fb_h - PASEK_H;
    int duzy = fb_w >= 1600;
    okna[O_TERMINAL].x = duzy ? (fb_w - ow(O_TERMINAL)) / 2 - 160 : (fb_w >= 1024 ? 200 : fb_w - ow(O_TERMINAL) - 8);
    okna[O_TERMINAL].y = duzy ? 90 : 20;
    okna[O_KALENDARZ].x = fb_w - ow(O_KALENDARZ) - 40;  okna[O_KALENDARZ].y = duzy ? 270 : 40;
    okna[O_ZEGAR].x = fb_w - ow(O_ZEGAR) - (duzy ? 460 : 60); okna[O_ZEGAR].y = duzy ? 140 : 60;
    okna[O_MUZYKA].x = (fb_w - ow(O_MUZYKA)) / 2 + 80;  okna[O_MUZYKA].y = duzy ? 200 : 90;
    okna[O_INFO].x = (fb_w - ow(O_INFO)) / 2;           okna[O_INFO].y = duzy ? 260 : 140;
    int www_w = fb_w - 300;
    if (www_w > 1240) www_w = 1240;
    if (www_w < 600) www_w = fb_w - 40;
    okna[O_WWW].cw = www_w;
    okna[O_WWW].ch = obszar_h - TB - BR - (duzy ? 60 : 16);
    okna[O_WWW].x = (fb_w - ow(O_WWW)) / 2 + (fb_w - ow(O_WWW) > 260 ? 80 : 0);
    okna[O_WWW].y = duzy ? 24 : 6;
    wwwg_init(okna[O_WWW].cw, okna[O_WWW].ch);
    okna[O_KALK].cw = kalk_szer(); okna[O_KALK].ch = kalk_wys();
    okna[O_KALK].x = fb_w - ow(O_KALK) - (duzy ? 440 : 40); okna[O_KALK].y = duzy ? 80 : 10;
    kalk_init(okna[O_KALK].cw, okna[O_KALK].ch);
    for (int a = 0; a < AP_N; a++) {
        okno_t *o = &okna[O_APKA + a];
        o->tytul = apki[a].tytul; o->ikona = apki[a].ikona;
        o->cw = apki[a].cw; o->ch = apki[a].ch;
        o->x = (fb_w - ow(O_APKA + a)) / 2 + (a - 3) * (duzy ? 70 : 24);
        o->y = (duzy ? 40 : 4) + a * (duzy ? 26 : 10);
    }
    for (int i = 0; i < O_N; i++) {
        if (okna[i].x < 0) okna[i].x = 0;
        if (okna[i].y + oh(i) > obszar_h) okna[i].y = obszar_h - oh(i) > 0 ? obszar_h - oh(i) : 0;
    }
    mx = fb_w / 2; my = fb_h / 2;
    mysz_init();
    okno_otworz(O_TERMINAL);
}

void gui_powitanie(void) {
    fb_klip_caly();
    fb_tapeta(0, 0, fb_w, fb_h);
    fb_prost_alfa(0, 0, fb_w, fb_h, 0x05081A, 110);
    int cx = fb_w / 2, cy = fb_h / 2 - 110;
    fb_kolo_aa(cx, cy, 110, 0xF6C640, 30);
    fb_kolo_aa(cx, cy, 70, 0xF6C640, 30);
    fb_zaokr(cx - 11, cy - 90, 22, 180, 6, 0xF6C640, 256);
    fb_zaokr(cx - 60, cy - 50, 120, 22, 6, 0xF6C640, 256);
    fb_napis_srodek(0, fb_w, cy + 120, "AmenOS", 0xFFFFFF, F_OGROMNY);
    char t[64]; ksprintf(t, sizeof t, "system operacyjny dla parafii · wersja %s", AMENOS_WERSJA);
    fb_napis_srodek(0, fb_w, cy + 214, t, 0xB8C6E8, F_SREDNI);
    fb_napis_srodek(0, fb_w, fb_h - 70, "Niech będzie pochwalony Jezus Chrystus", 0xF6DE8E, F_TEKST_B);
    fb_wyslij(0, 0, fb_w, fb_h);
    /* kropki ładowania */
    int ky = cy + 270;
    for (int krok = 0; krok < 16; krok++) {
        fb_klip(cx - 60, ky - 10, 120, 20);
        fb_tapeta(cx - 60, ky - 10, 120, 20);
        fb_prost_alfa(cx - 60, ky - 10, 120, 20, 0x05081A, 110);
        for (int k = 0; k < 5; k++) fb_kolo_aa(cx - 40 + k * 20, ky, 4, 0xFFFFFF, k == krok % 5 ? 256 : 70);
        fb_wyslij(cx - 60, ky - 10, 120, 20);
        czekaj_ms(150);
    }
    fb_klip_caly();
    for (int i = 0; i < 64; i++) ps2_obsluz();
    dzw_efekt(E_START);
    gui_aktywne = 1;
    brudne_wszystko();
}
