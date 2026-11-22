/* AmenOS - pulpit graficzny: okna, ikony, pasek zadań, menu Start i programy graficzne.
 *
 * System ma jeden "wątek": powłokę tekstową. Pulpit działa przy okazji - za każdym razem,
 * gdy jakikolwiek program czeka na klawisz (kbd_poll) albo odlicza czas (czekaj_ms),
 * wołana jest gui_pompuj(), która obsługuje mysz, okna i rysowanie. Powłoka i wszystkie
 * programy tekstowe działają w oknie Terminal. */
#include "kernel.h"
#include "muzyka_dane.h"

int gui_aktywne;
int powloka_czeka;

#define PASEK_H 36                         /* pasek zadań */
#define TB      26                         /* belka tytułowa okna */
#define BR      2                          /* ramka okna */

static u32 pamiec_kb;

/* ---------- kolory ---------- */

static const u32 PALETA16[16] = { 0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                                  0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };
static u32 kolor_lit(int k) {
    static const u32 t[] = { 0x2E7D32, 0x6A1B9A, 0xF4F1E8, 0xC62828, 0xE88AB0 };
    return t[k];
}
static u32 tekst_na_lit(int k) { return (k == KOL_BIALY || k == KOL_ROZOWY) ? 0x202020 : 0xFFFFFF; }

#define TLO_OKNA   0xF3F1EC
#define CIEMNY     0x1E2230
#define SZARY_T    0x6A6F7A

/* ---------- okna ---------- */

enum { O_TERMINAL, O_KALENDARZ, O_ZEGAR, O_MUZYKA, O_INFO, O_WWW, O_KALK, O_KOMUNIKAT, O_N };
typedef struct { int x, y, cw, ch; const char *tytul; int ikona, otwarte, zminim; } okno_t;
static okno_t okna[O_N] = {
    { 0, 0, 640, 400, "Terminal",              0, 0, 0 },
    { 0, 0, 336, 392, "Kalendarz liturgiczny", 1, 0, 0 },
    { 0, 0, 260, 330, "Zegar",                 2, 0, 0 },
    { 0, 0, 440, 336, "Muzyka mszalna",        4, 0, 0 },
    { 0, 0, 420, 236, "O systemie",           11, 0, 0 },
    { 0, 0, 860, 660, "Przeglądarka",          5, 0, 0 },
    { 0, 0, 326, 540, "Kalkulator biblijny",  12, 0, 0 },
    { 0, 0, 380, 132, "AmenOS",               11, 0, 0 },
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

static czas_t dzis;
static dzien_lit_t dzis_lit;

static void brudne(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (!brud) { bx0 = x; by0 = y; bx1 = x + w; by1 = y + h; brud = 1; return; }
    if (x < bx0) bx0 = x;
    if (y < by0) by0 = y;
    if (x + w > bx1) bx1 = x + w;
    if (y + h > by1) by1 = y + h;
}
static void brudne_wszystko(void) { brudne(0, 0, fb_w, fb_h); }

static int ow(int i) { return okna[i].cw + 2 * BR; }
static int oh(int i) { return okna[i].ch + TB + BR; }
static int widoczne(int i) { return okna[i].otwarte && !okna[i].zminim; }
static void okno_brudne(int i) { brudne(okna[i].x, okna[i].y, ow(i) + 8, oh(i) + 8); }

static int okno_aktywne(void) {
    for (int k = zn - 1; k >= 0; k--) if (widoczne(z[k])) return z[k];
    return -1;
}

static void okno_podnies(int i) {
    int k = 0;
    while (k < zn && z[k] != i) k++;
    if (k == zn) { z[zn++] = i; }
    else { for (; k < zn - 1; k++) z[k] = z[k + 1]; z[zn - 1] = i; }
    okno_brudne(i);
    brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);
}

static void okno_otworz(int i) {
    okna[i].otwarte = 1;
    okna[i].zminim = 0;
    okno_podnies(i);
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
    brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);
}

static void komunikat(const char *s) {
    k_strncpy(komunikat_txt, s, sizeof komunikat_txt);
    okna[O_KOMUNIKAT].x = (fb_w - ow(O_KOMUNIKAT)) / 2;
    okna[O_KOMUNIKAT].y = (fb_h - PASEK_H - oh(O_KOMUNIKAT)) / 2;
    okno_otworz(O_KOMUNIKAT);
}

int gui_terminal_aktywny(void) { return !menu_otwarte && okno_aktywne() == O_TERMINAL; }

void gui_www_brudne(int x, int y, int w, int h) {
    if (widoczne(O_WWW)) brudne(okna[O_WWW].x + BR + x, okna[O_WWW].y + TB + y, w, h);
    if (y < 0 && okna[O_WWW].otwarte) brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);   /* nowy tytuł na pasku zadań */
}
static void komponuj(void);
void gui_rysuj_teraz(void) { if (brud) komponuj(); }
void gui_kalk_brudne(int x, int y, int w, int h) {
    if (widoczne(O_KALK)) brudne(okna[O_KALK].x + BR + x, okna[O_KALK].y + TB + y, w, h);
}
int gui_www_aktywne(void) { return !menu_otwarte && okno_aktywne() == O_WWW; }

/* ---------- elementy wspólne ---------- */

static void przycisk(int x, int y, int w, int h, const char *t, int wazny) {
    fb_prost_zaokr(x, y, w, h, 4, wazny ? 0x2C5AA0 : 0xC9CDD6);
    fb_prost_zaokr(x + 1, y + 1, w - 2, h - 2, 3, wazny ? 0x3A6EC0 : 0xE8EAEF);
    fb_tekst(x + (w - fb_tekst_szer(t, 1)) / 2, y + (h - 16) / 2, t, wazny ? 0xFFFFFF : 0x202020, FB_PRZEZR, 1);
}

static int w_prost(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

static void tekst_srodek(int x, int w, int y, const char *s, u32 c, int skala) {
    fb_tekst(x + (w - fb_tekst_szer(s, skala)) / 2, y, s, c, FB_PRZEZR, skala);
}

static void tekst_gruby(int x, int y, const char *s, u32 c) {
    fb_tekst(x, y, s, c, FB_PRZEZR, 1);
    fb_tekst(x + 1, y, s, c, FB_PRZEZR, 1);
}

/* ====================================================================== */
/*  Terminal                                                               */
/* ====================================================================== */

static u16 term_pokazane[VGA_W * VGA_H];
static int kur_x = -1, kur_y = -1, kur_stan;

static void rysuj_terminal(int cx, int cy) {
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    int c0 = (kx - cx) / 8, c1 = (kx + kw - cx + 7) / 8, r0 = (ky - cy) / 16, r1 = (ky + kh - cy + 15) / 16;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > VGA_W) c1 = VGA_W;
    if (r1 > VGA_H) r1 = VGA_H;
    const u16 *b = vga_bufor();
    for (int r = r0; r < r1; r++)
        for (int c = c0; c < c1; c++) {
            u16 v = b[r * VGA_W + c];
            fb_znak(cx + c * 8, cy + r * 16, (u8)v, PALETA16[(v >> 8) & 15], PALETA16[(v >> 12) & 15], 1);
        }
    if (kur_stan && kur_x >= 0) {
        u16 v = b[kur_y * VGA_W + kur_x];
        fb_prost(cx + kur_x * 8, cy + kur_y * 16 + 13, 8, 2, PALETA16[(v >> 8) & 15]);
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
    int ox = okna[O_TERMINAL].x + BR, oy = okna[O_TERMINAL].y + TB;
    int widac = widoczne(O_TERMINAL);
    if (r1 >= 0 && widac) brudne(ox + c0 * 8, oy + r0 * 16, (c1 - c0 + 1) * 8, (r1 - r0 + 1) * 16);
    int stan = widac && gui_terminal_aktywny() && vga_kursor_widoczny() && ((t / 500) & 1) == 0;
    int nx = vga_x(), ny = vga_y();
    if (nx >= VGA_W) nx = VGA_W - 1;
    if (ny >= VGA_H) ny = VGA_H - 1;
    if (stan != kur_stan || nx != kur_x || ny != kur_y) {
        if (kur_x >= 0) brudne(ox + kur_x * 8, oy + kur_y * 16, 8, 16);
        kur_stan = stan; kur_x = nx; kur_y = ny;
        brudne(ox + kur_x * 8, oy + kur_y * 16, 8, 16);
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

static void rysuj_kalendarz(int x, int y) {
    int w = okna[O_KALENDARZ].cw;
    dzien_lit_t wl;
    liturgia(kal_r, kal_m, kal_d, &wl);
    fb_prost(x, y, w, 38, kolor_lit(wl.kolor));
    u32 tn = tekst_na_lit(wl.kolor);
    char t[120];
    ksprintf(t, sizeof t, "%s %d", mies_mian[kal_m], kal_r);
    int tx = x + (w - fb_tekst_szer(t, 1)) / 2;
    tekst_gruby(tx, y + 11, t, tn);
    przycisk(x + 8, y + 7, 28, 24, "<", 0);
    przycisk(x + w - 36, y + 7, 28, 24, ">", 0);

    static const char *nagl[7] = { "Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd" };
    for (int i = 0; i < 7; i++) tekst_srodek(x + 7 + i * 46, 46, y + 46, nagl[i], i == 6 ? 0xC62828 : SZARY_T, 1);

    int pierwszy = dni_od_epoki(kal_r, kal_m, 1);
    int kol = (dzien_tygodnia(pierwszy) + 6) % 7, wiersz = 0;
    for (int d = 1; d <= dni_w_miesiacu(kal_r, kal_m); d++) {
        dzien_lit_t l; liturgia(kal_r, kal_m, d, &l);
        int cx = x + 7 + kol * 46, cy = y + 66 + wiersz * 32;
        fb_prost(cx + 1, cy + 1, 44, 30, kolor_lit(l.kolor));
        if (l.kolor == KOL_BIALY) fb_ramka(cx + 1, cy + 1, 44, 30, 0xD8D4C8);
        char n[4]; ksprintf(n, sizeof n, "%d", d);
        tekst_srodek(cx, 46, cy + 8, n, tekst_na_lit(l.kolor), 1);
        if (l.ranga <= 2) fb_kolo(cx + 39, cy + 6, 2, tekst_na_lit(l.kolor));
        if (d == dzis.dzien && kal_m == dzis.mies && kal_r == dzis.rok) {
            fb_ramka(cx, cy, 46, 32, 0xF2C230); fb_ramka(cx + 1, cy + 1, 44, 30, 0xF2C230);
        }
        if (d == kal_d) fb_ramka(cx + 3, cy + 3, 40, 26, 0x101018);
        if (++kol == 7) { kol = 0; wiersz++; }
    }

    int iy = y + 266;
    fb_prost(x + 8, iy - 6, w - 16, 1, 0xD0CCC2);
    int dow = dzien_tygodnia(dni_od_epoki(kal_r, kal_m, kal_d));
    ksprintf(t, sizeof t, "%s, %d %s %d", dni_tyg_wielk[dow], kal_d, mies_dop[kal_m], kal_r);
    tekst_gruby(x + 10, iy, t, 0x202020);
    int lin = fb_tekst_zawin(x + 10, iy + 20, wl.nazwa, 39, 0x1C3A70, 17);
    int yy = iy + 20 + lin * 17;
    if (wl.uwaga[0]) { ksprintf(t, sizeof t, "(%s)", wl.uwaga); yy += fb_tekst_zawin(x + 10, yy, t, 39, SZARY_T, 17) * 17; }
    fb_prost(x + 10, yy + 4, 12, 12, kolor_lit(wl.kolor));
    fb_ramka(x + 10, yy + 4, 12, 12, 0x808080);
    ksprintf(t, sizeof t, "%s · %s · rok %c/%s", wl.okres, kolor_nazwa(wl.kolor), wl.cykl_niedz, wl.cykl_powsz == 1 ? "I" : "II");
    fb_tekst(x + 28, yy + 2, t, 0x404040, FB_PRZEZR, 1);
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
    int w = okna[O_KALENDARZ].cw;
    if (w_prost(lx, ly, 8, 7, 28, 24)) { kal_zmien_mies(-1); return; }
    if (w_prost(lx, ly, w - 36, 7, 28, 24)) { kal_zmien_mies(1); return; }
    if (ly >= 66 && ly < 66 + 6 * 32 && lx >= 7 && lx < 7 + 7 * 46) {
        int kol = (lx - 7) / 46, wiersz = (ly - 66) / 32;
        int pierwszy = (dzien_tygodnia(dni_od_epoki(kal_r, kal_m, 1)) + 6) % 7;
        int d = wiersz * 7 + kol - pierwszy + 1;
        if (d >= 1 && d <= dni_w_miesiacu(kal_r, kal_m)) { kal_d = d; okno_brudne(O_KALENDARZ); }
    }
}

/* ====================================================================== */
/*  Zegar                                                                  */
/* ====================================================================== */

static void wskazowka(int cx, int cy, int poz, int dl, int gr, u32 c) {
    fb_linia(cx, cy, cx + fb_sinus(poz) * dl / 1000, cy - fb_sinus(poz + 15) * dl / 1000, c, gr);
}

static void rysuj_zegar(int x, int y) {
    int w = okna[O_ZEGAR].cw;
    czas_t c; rtc_czytaj(&c);
    int cx = x + w / 2, cy = y + 128, r = 110;
    fb_kolo(cx + 3, cy + 4, r + 4, 0xD8D4CA);
    fb_kolo(cx, cy, r + 4, 0x2C5AA0);
    fb_kolo(cx, cy, r - 2, 0xFFFDF6);
    for (int i = 0; i < 60; i++) {
        int dl = (i % 5) ? 6 : 14, gr = (i % 5) ? 1 : 3;
        int x0 = cx + fb_sinus(i) * (r - 6) / 1000, y0 = cy - fb_sinus(i + 15) * (r - 6) / 1000;
        int x1 = cx + fb_sinus(i) * (r - 6 - dl) / 1000, y1 = cy - fb_sinus(i + 15) * (r - 6 - dl) / 1000;
        fb_linia(x0, y0, x1, y1, (i % 5) ? 0x9A9A9A : 0x202838, gr);
    }
    static const char *cyfry[4] = { "12", "3", "6", "9" };
    for (int i = 0; i < 4; i++) {
        int px = cx + fb_sinus(i * 15) * (r - 34) / 1000, py = cy - fb_sinus(i * 15 + 15) * (r - 34) / 1000;
        fb_tekst(px - fb_tekst_szer(cyfry[i], 1) / 2, py - 8, cyfry[i], 0x202838, FB_PRZEZR, 1);
    }
    fb_tekst(cx - 24, cy + 30, "AmenOS", 0xB0A890, FB_PRZEZR, 1);
    wskazowka(cx, cy, (c.godz % 12) * 5 + c.min / 12, 56, 5, 0x202838);
    wskazowka(cx, cy, c.min, 84, 3, 0x202838);
    wskazowka(cx, cy, c.sek, 92, 1, 0xC62828);
    fb_kolo(cx, cy, 5, 0xC62828);
    char t[64];
    ksprintf(t, sizeof t, "%02d:%02d:%02d", c.godz, c.min, c.sek);
    tekst_srodek(x, w, y + 254, t, 0x202838, 2);
    ksprintf(t, sizeof t, "%s, %d %s", dni_tyg[dzien_tygodnia(dni_od_epoki(c.rok, c.mies, c.dzien))], c.dzien, mies_dop[c.mies]);
    tekst_srodek(x, w, y + 296, t, SZARY_T, 1);
}

/* ====================================================================== */
/*  Muzyka mszalna - odtwarzanie w tle                                     */
/* ====================================================================== */

static int muz_wybr, muz_gra = -1, muz_nuta;
static u32 muz_koniec, muz_ton_do, muz_start, muz_calosc, muz_ost_rys;
static int muz_ton;

static void muzyka_stop(void) {
    if (muz_gra >= 0) glosnik_cisza();
    muz_gra = -1; muz_ton = 0;
    if (okna[O_MUZYKA].otwarte) okno_brudne(O_MUZYKA);
    brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);
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
    muzyka_nuta(muz_start);
    okno_brudne(O_MUZYKA);
    brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);
}

static void muzyka_tik(u32 t) {
    if (muz_gra < 0) return;
    if (muz_ton && (int)(t - muz_ton_do) >= 0) { glosnik_cisza(); muz_ton = 0; }
    if ((int)(t - muz_koniec) >= 0) {
        if (++muz_nuta >= utwory[muz_gra].n) { muzyka_stop(); return; }
        muzyka_nuta(t);
    }
    if (okna[O_MUZYKA].otwarte && t - muz_ost_rys > 250) {
        muz_ost_rys = t;
        brudne(okna[O_MUZYKA].x + BR, okna[O_MUZYKA].y + TB + 270, okna[O_MUZYKA].cw, 60);
    }
}

static void rysuj_muzyke(int x, int y) {
    int w = okna[O_MUZYKA].cw;
    for (int i = 0; i < UTWORY_N; i++) {
        int ry = y + 8 + i * 30;
        if (i == muz_wybr) fb_prost_zaokr(x + 6, ry, w - 12, 28, 4, 0xC8DAF5);
        u32 kol = (i == muz_gra) ? 0x1E7A30 : 0x202020;
        char n[8]; ksprintf(n, sizeof n, "%d.", i + 1);
        fb_tekst(x + 14, ry + 6, n, SZARY_T, FB_PRZEZR, 1);
        fb_tekst(x + 40, ry + 6, utwory[i].tytul, kol, FB_PRZEZR, 1);
        if (i == muz_gra) fb_tekst(x + 22 + fb_tekst_szer(utwory[i].tytul, 1) + 24, ry + 6, "♫", 0x1E7A30, FB_PRZEZR, 1);
        int sz = fb_tekst_szer(utwory[i].czesc, 1);
        fb_tekst(x + w - 14 - sz, ry + 6, utwory[i].czesc, SZARY_T, FB_PRZEZR, 1);
    }
    przycisk(x + 10, y + 252, 110, 30, "► Graj", 1);
    przycisk(x + 130, y + 252, 110, 30, "■ Stop", 0);
    fb_tekst(x + 254, y + 259, "Enter - graj", SZARY_T, FB_PRZEZR, 1);
    int py = y + 296;
    fb_prost_zaokr(x + 10, py, w - 20, 10, 4, 0xD8DCE4);
    if (muz_gra >= 0 && muz_calosc) {
        u32 minelo = zegar_ms() - muz_start;
        if (minelo > muz_calosc) minelo = muz_calosc;
        int pel = (int)((w - 20) * minelo / muz_calosc);
        if (pel > 8) fb_prost_zaokr(x + 10, py, pel, 10, 4, 0x3A6EC0);
        char t[96];
        ksprintf(t, sizeof t, "Gra: %s   %d:%02d / %d:%02d", utwory[muz_gra].tytul, (int)(minelo / 60000), (int)(minelo / 1000 % 60),
                 (int)(muz_calosc / 60000), (int)(muz_calosc / 1000 % 60));
        fb_tekst(x + 10, py + 16, t, 0x404040, FB_PRZEZR, 1);
    } else fb_tekst(x + 10, py + 16, "Wybierz utwór i naciśnij Graj. Muzyka gra w tle.", SZARY_T, FB_PRZEZR, 1);
}

static void klik_muzyka(int lx, int ly) {
    if (ly >= 8 && ly < 8 + UTWORY_N * 30) {
        int i = (ly - 8) / 30;
        u32 t = zegar_ms();
        if (i == muz_wybr && t - ost_klik_czas < 450) muzyka_graj(i);
        muz_wybr = i; ost_klik_czas = t;
        okno_brudne(O_MUZYKA);
    } else if (w_prost(lx, ly, 10, 252, 110, 30)) muzyka_graj(muz_wybr);
    else if (w_prost(lx, ly, 130, 252, 110, 30)) muzyka_stop();
}

/* ====================================================================== */
/*  O systemie, komunikat                                                  */
/* ====================================================================== */

static void rysuj_info(int x, int y) {
    fb_ikona(x + 16, y + 20, fb_ikona_nr(11), 5);
    tekst_gruby(x + 112, y + 20, "AmenOS " AMENOS_WERSJA, 0x1C3A70);
    fb_tekst(x + 112, y + 40, "System operacyjny dla parafii", 0x404040, FB_PRZEZR, 1);
    char t[96], cpu[52];
    u32 r[12], a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (a >= 0x80000004u) {
        for (u32 i = 0; i < 3; i++)
            __asm__ volatile("cpuid" : "=a"(r[i * 4]), "=b"(r[i * 4 + 1]), "=c"(r[i * 4 + 2]), "=d"(r[i * 4 + 3]) : "a"(0x80000002u + i), "c"(0));
        memcpy(cpu, r, 48); cpu[48] = 0;
    } else k_strncpy(cpu, "x86", sizeof cpu);
    char *p = cpu; while (*p == ' ') p++;
    p[34] = 0;
    int ly = y + 80;
    ksprintf(t, sizeof t, "Ekran:     %d x %d", fb_w, fb_h);       fb_tekst(x + 112, ly, t, 0x202020, FB_PRZEZR, 1); ly += 20;
    ksprintf(t, sizeof t, "Pamięć:    %d MB", (int)(pamiec_kb / 1024)); fb_tekst(x + 112, ly, t, 0x202020, FB_PRZEZR, 1); ly += 20;
    ksprintf(t, sizeof t, "Procesor:  %s", p);                       fb_tekst(x + 112, ly, t, 0x202020, FB_PRZEZR, 1); ly += 20;
    if (siec_gotowa) { char ip[16]; ip_tekst(moje_ip, ip); ksprintf(t, sizeof t, "Sieć:      %s", ip); }
    else k_strncpy(t, "Sieć:      nieuruchomiona", sizeof t);
    fb_tekst(x + 112, ly, t, 0x202020, FB_PRZEZR, 1);
    fb_tekst(x + 112, y + 200, "Niech będzie pochwalony Jezus Chrystus", 0x8A6A20, FB_PRZEZR, 1);
}

static void rysuj_komunikat(int x, int y) {
    int w = okna[O_KOMUNIKAT].cw;
    fb_ikona(x + 14, y + 16, fb_ikona_nr(11), 2);
    fb_tekst_zawin(x + 56, y + 16, komunikat_txt, 38, 0x202020, 18);
    przycisk(x + w - 96, y + okna[O_KOMUNIKAT].ch - 40, 84, 28, "OK", 1);
}

/* ====================================================================== */
/*  Ikony, menu Start, uruchamianie                                        */
/* ====================================================================== */

enum { A_TERMINAL, A_KALENDARZ, A_ZEGAR, A_PIESNI, A_MUZYKA, A_WWW, A_MODLITWY, A_SPOWIEDZ, A_KOMUNIA,
       A_OGLOSZENIA, A_MSZE, A_INFO, A_DZIS, A_RESTART, A_WYLACZ, A_ODSTEP, A_WWW_TEKST, A_KALK };

static const struct { const char *nazwa; int ikona, akcja; } ikony[] = {
    { "Terminal", 0, A_TERMINAL }, { "Kalendarz", 1, A_KALENDARZ }, { "Zegar", 2, A_ZEGAR },
    { "Pieśni", 3, A_PIESNI }, { "Muzyka", 4, A_MUZYKA }, { "Internet", 5, A_WWW },
    { "Modlitwy", 6, A_MODLITWY }, { "Spowiedź", 7, A_SPOWIEDZ }, { "Komunia", 8, A_KOMUNIA },
    { "Ogłoszenia", 9, A_OGLOSZENIA }, { "Msze", 10, A_MSZE }, { "Kalkulator", 12, A_KALK },
    { "O systemie", 11, A_INFO },
};
#define IKONY_N ((int)(sizeof ikony / sizeof ikony[0]))
#define IK_W 84
#define IK_H 84

static void ikona_poz(int i, int *x, int *y) {
    int na_kol = (fb_h - PASEK_H - 16) / IK_H;
    *x = 12 + (i / na_kol) * IK_W;
    *y = 12 + (i % na_kol) * IK_H;
}

static const struct { const char *nazwa; int ikona, akcja; } menu[] = {
    { "Terminal", 0, A_TERMINAL }, { "Dziś w liturgii", 1, A_DZIS }, { "Kalendarz liturgiczny", 1, A_KALENDARZ },
    { "Zegar", 2, A_ZEGAR }, { "Tablica pieśni", 3, A_PIESNI }, { "Muzyka mszalna", 4, A_MUZYKA },
    { "Przeglądarka", 5, A_WWW }, { "Przeglądarka tekstowa", 0, A_WWW_TEKST },
    { "Modlitwy", 6, A_MODLITWY }, { "Spowiedź", 7, A_SPOWIEDZ },
    { "Komunia święta", 8, A_KOMUNIA }, { "Ogłoszenia", 9, A_OGLOSZENIA }, { "Porządek Mszy", 10, A_MSZE },
    { "Kalkulator biblijny", 12, A_KALK },
    { "O systemie", 11, A_INFO }, { 0, -1, A_ODSTEP },
    { "Uruchom ponownie", -1, A_RESTART }, { "Wyłącz komputer", -1, A_WYLACZ },
};
#define MENU_N ((int)(sizeof menu / sizeof menu[0]))
#define MENU_W 250

static int menu_wys(void) { int h = 12; for (int i = 0; i < MENU_N; i++) h += menu[i].akcja == A_ODSTEP ? 9 : 28; return h; }

static void wylacz(void) {
    glosnik_cisza();
    outw(0x604, 0x2000); outw(0xB004, 0x2000); outw(0x4004, 0x3400);
    fb_klip_caly();
    fb_gradient(0, 0, fb_w, fb_h, 0x1A2A50, 0x0A1020);
    tekst_srodek(0, fb_w, fb_h / 2 - 40, "Szczęść Boże!", 0xF2C230, 3);
    tekst_srodek(0, fb_w, fb_h / 2 + 20, "Można bezpiecznie wyłączyć komputer.", 0xFFFFFF, 1);
    fb_wyslij(0, 0, fb_w, fb_h);
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

static void uruchom(int a) {
    switch (a) {
    case A_TERMINAL:   okno_otworz(O_TERMINAL); break;
    case A_KALENDARZ:  kal_r = dzis.rok; kal_m = dzis.mies; kal_d = dzis.dzien; okno_otworz(O_KALENDARZ); break;
    case A_ZEGAR:      okno_otworz(O_ZEGAR); break;
    case A_MUZYKA:     okno_otworz(O_MUZYKA); break;
    case A_INFO:       okno_otworz(O_INFO); break;
    case A_DZIS:       polecenie("dzis"); break;
    case A_PIESNI:     polecenie("piesni"); break;
    case A_WWW:
        okno_otworz(O_WWW);
        if (!www_start) { www_start = 1; wwwg_otworz("about:start"); }
        break;
    case A_WWW_TEKST:  polecenie("www"); break;
    case A_KALK:       okno_otworz(O_KALK); break;
    case A_MODLITWY:   polecenie("modlitwy"); break;
    case A_SPOWIEDZ:   polecenie("spowiedz"); break;
    case A_KOMUNIA:    polecenie("komunia"); break;
    case A_OGLOSZENIA: polecenie("ogloszenia"); break;
    case A_MSZE:       polecenie("msze"); break;
    case A_RESTART:    while (inb(0x64) & 2) ; outb(0x64, 0xFE); break;
    case A_WYLACZ:     wylacz(); break;
    }
}

/* ====================================================================== */
/*  Rysowanie całości                                                      */
/* ====================================================================== */

static void rysuj_pulpit(void) {
    int h = fb_h - PASEK_H;
    fb_gradient(0, 0, fb_w, h, 0x1E3A66, 0x0B1428);
    fb_tekst(fb_w - fb_tekst_szer("AmenOS", 3) - 24, h - 72, "AmenOS", 0x3A5A8A, FB_PRZEZR, 3);
    fb_tekst(fb_w - fb_tekst_szer("system dla parafii", 1) - 26, h - 24, "system dla parafii", 0x5A7AAA, FB_PRZEZR, 1);

    /* obchód dnia w prawym górnym rogu */
    char t[120];
    ksprintf(t, sizeof t, "%s, %d %s %d", dni_tyg_wielk[dzien_tygodnia(dni_od_epoki(dzis.rok, dzis.mies, dzis.dzien))],
             dzis.dzien, mies_dop[dzis.mies], dzis.rok);
    int tx = fb_w - fb_tekst_szer(t, 1) - 24;
    fb_tekst(tx + 1, 17, t, 0x000000, FB_PRZEZR, 1);
    fb_tekst(tx, 16, t, 0xFFFFFF, FB_PRZEZR, 1);
    int nx = fb_w - fb_tekst_szer(dzis_lit.nazwa, 1) - 24;
    if (nx < 200) nx = 200;
    fb_prost(nx - 18, 38, 10, 10, kolor_lit(dzis_lit.kolor));
    fb_ramka(nx - 18, 38, 10, 10, 0xFFFFFF);
    fb_tekst(nx + 1, 35, dzis_lit.nazwa, 0x000000, FB_PRZEZR, 1);
    fb_tekst(nx, 34, dzis_lit.nazwa, 0xF2D27A, FB_PRZEZR, 1);

    for (int i = 0; i < IKONY_N; i++) {
        int x, y; ikona_poz(i, &x, &y);
        if (i == wybrana_ikona) fb_prost_alfa(x, y, IK_W - 4, IK_H - 4, 0x8CC8F4, 70);
        fb_ikona(x + (IK_W - 4 - 48) / 2, y + 6, fb_ikona_nr(ikony[i].ikona), 3);
        int sz = fb_tekst_szer(ikony[i].nazwa, 1), lx = x + (IK_W - 4 - sz) / 2;
        fb_tekst(lx + 1, y + 59, ikony[i].nazwa, 0x000000, FB_PRZEZR, 1);
        fb_tekst(lx, y + 58, ikony[i].nazwa, 0xFFFFFF, FB_PRZEZR, 1);
    }
}

static void rysuj_okno(int i) {
    okno_t *o = &okna[i];
    int x = o->x, y = o->y, w = ow(i), h = oh(i);
    int akt = (i == okno_aktywne());
    fb_prost_alfa(x + 6, y + 6, w, h, 0x000000, 70);
    fb_prost(x, y, w, h, akt ? 0x1C3A70 : 0x5A5F6A);
    if (akt) fb_gradient(x + 1, y + 1, w - 2, TB - 2, 0x3A6EC0, 0x24508F);
    else fb_gradient(x + 1, y + 1, w - 2, TB - 2, 0x9AA0AC, 0x7A808C);
    fb_ikona(x + 6, y + 5, fb_ikona_nr(o->ikona), 1);
    if (i == O_WWW) o->tytul = wwwg_tytul();
    {
        char t[160]; const char *q = o->tytul;
        int maks = (w - 90) / 8, n = 0;
        while (*q && n < maks) { k_utf8_next(&q); n++; }
        u32 dl = (u32)(q - o->tytul);
        if (dl > sizeof t - 1) dl = sizeof t - 1;
        memcpy(t, o->tytul, dl); t[dl] = 0;
        tekst_gruby(x + 28, y + 5, t, 0xFFFFFF);
    }
    /* przyciski: minimalizuj i zamknij */
    int bx = x + w - BR - 24;
    fb_prost_zaokr(bx, y + 4, 20, 18, 3, akt ? 0xC8463C : 0x8A8F9A);
    fb_linia(bx + 6, y + 9, bx + 13, y + 16, 0xFFFFFF, 2);
    fb_linia(bx + 13, y + 9, bx + 6, y + 16, 0xFFFFFF, 2);
    if (i != O_KOMUNIKAT) {
        fb_prost_zaokr(bx - 24, y + 4, 20, 18, 3, akt ? 0x4A7ED0 : 0x8A8F9A);
        fb_prost(bx - 19, y + 15, 10, 2, 0xFFFFFF);
    }
    int cx = x + BR, cy = y + TB;
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    fb_klip_zawez(cx, cy, o->cw, o->ch);
    fb_prost(cx, cy, o->cw, o->ch, i == O_TERMINAL ? 0x000000 : TLO_OKNA);
    switch (i) {
    case O_TERMINAL:  rysuj_terminal(cx, cy); break;
    case O_KALENDARZ: rysuj_kalendarz(cx, cy); break;
    case O_ZEGAR:     rysuj_zegar(cx, cy); break;
    case O_MUZYKA:    rysuj_muzyke(cx, cy); break;
    case O_INFO:      rysuj_info(cx, cy); break;
    case O_KOMUNIKAT: rysuj_komunikat(cx, cy); break;
    case O_WWW:       wwwg_rysuj(cx, cy); break;
    case O_KALK:      kalk_rysuj(cx, cy); break;
    }
    fb_klip(kx, ky, kw, kh);
}

static void rysuj_pasek(void) {
    int y = fb_h - PASEK_H;
    fb_gradient(0, y, fb_w, PASEK_H, 0x2E3242, 0x181A24);
    fb_prost(0, y, fb_w, 1, 0x50566E);
    /* przycisk Start */
    fb_prost_zaokr(4, y + 4, 118, 28, 5, menu_otwarte ? 0x24508F : 0x3A6EC0);
    fb_ikona(10, y + 10, fb_ikona_nr(11), 1);
    tekst_gruby(32, y + 10, "AmenOS", 0xFFFFFF);
    /* przyciski okien */
    int bx = 132;
    int akt = okno_aktywne();
    for (int i = 0; i < O_KOMUNIKAT; i++) {
        if (!okna[i].otwarte) continue;
        if (i == O_WWW) okna[i].tytul = wwwg_tytul();
        u32 c = (i == akt) ? 0x4A5878 : 0x323848;
        fb_prost_zaokr(bx, y + 4, 168, 28, 4, c);
        fb_ikona(bx + 6, y + 10, fb_ikona_nr(okna[i].ikona), 1);
        char t[24]; k_strncpy(t, okna[i].tytul, 18);
        fb_tekst(bx + 28, y + 10, t, okna[i].zminim ? 0x9AA0B0 : 0xFFFFFF, FB_PRZEZR, 1);
        bx += 174;
    }
    /* zasobnik: muzyka, kolor dnia, godzina */
    char t[32];
    ksprintf(t, sizeof t, "%02d:%02d", dzis.godz, dzis.min);
    int tx = fb_w - 70;
    tekst_gruby(tx, y + 3, t, 0xFFFFFF);
    ksprintf(t, sizeof t, "%02d.%02d.%d", dzis.dzien, dzis.mies, dzis.rok);
    fb_tekst(fb_w - 88, y + 18, t, 0xA8B0C4, FB_PRZEZR, 1);
    fb_kolo(fb_w - 106, y + 18, 7, kolor_lit(dzis_lit.kolor));
    fb_okrag(fb_w - 106, y + 18, 7, 0xFFFFFF, 1);
    if (muz_gra >= 0) fb_tekst(fb_w - 132, y + 10, "♫", 0xF2C230, FB_PRZEZR, 1);
}

static void rysuj_menu(void) {
    int h = menu_wys(), x = 0, y = fb_h - PASEK_H - h;
    fb_prost_alfa(x + 6, y + 6, MENU_W, h, 0x000000, 80);
    fb_prost(x, y, MENU_W, h, 0xF3F1EC);
    fb_ramka(x, y, MENU_W, h, 0x1C3A70);
    fb_gradient(x + 1, y + 1, 30, h - 2, kolor_lit(dzis_lit.kolor), fb_mieszaj(kolor_lit(dzis_lit.kolor), 0x000000, 120));
    fb_ikona(x + 8, y + h - 26, fb_ikona_nr(11), 1);
    int iy = y + 6;
    for (int i = 0; i < MENU_N; i++) {
        if (menu[i].akcja == A_ODSTEP) { fb_prost(x + 40, iy + 4, MENU_W - 50, 1, 0xC8C4BA); iy += 9; continue; }
        if (w_prost(mx, my, x + 32, iy, MENU_W - 34, 28)) fb_prost(x + 32, iy, MENU_W - 34, 28, 0xC8DAF5);
        if (menu[i].ikona >= 0) fb_ikona(x + 40, iy + 6, fb_ikona_nr(menu[i].ikona), 1);
        fb_tekst(x + 66, iy + 6, menu[i].nazwa, 0x202020, FB_PRZEZR, 1);
        iy += 28;
    }
}

static const char *KURSOR[19] = {
    "X...........", "XX..........", "XWX.........", "XWWX........", "XWWWX.......", "XWWWWX......",
    "XWWWWWX.....", "XWWWWWWX....", "XWWWWWWWX...", "XWWWWWWWWX..", "XWWWWWWWWWX.", "XWWWWWWXXXXX",
    "XWWWXWWX....", "XWWXXWWX....", "XWX..XWWX...", "XX...XWWX...", "X.....XWWX..", "......XWWX..", ".......XX..." };

static void rysuj_kursor(void) {
    for (int j = 0; j < 19; j++)
        for (int i = 0; i < 12; i++) {
            char c = KURSOR[j][i];
            if (c == 'X') fb_piksel(mx + i, my + j, 0x000000);
            else if (c == 'W') fb_piksel(mx + i, my + j, 0xFFFFFF);
        }
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
    rysuj_kursor();
    fb_klip_caly();
    fb_wyslij(x, y, w, h);
}

/* ====================================================================== */
/*  Mysz i klawiatura                                                      */
/* ====================================================================== */

static void klik_pasek(int x) {
    if (x < 126) { menu_otwarte = !menu_otwarte; brudne_wszystko(); return; }
    int bx = 132;
    for (int i = 0; i < O_KOMUNIKAT; i++) {
        if (!okna[i].otwarte) continue;
        if (x >= bx && x < bx + 168) {
            if (okno_aktywne() == i) { okna[i].zminim = 1; okno_brudne(i); brudne(0, fb_h - PASEK_H, fb_w, PASEK_H); }
            else { okna[i].zminim = 0; okno_podnies(i); }
            return;
        }
        bx += 174;
    }
}

static void klik(int x, int y) {
    u32 t = zegar_ms();
    if (menu_otwarte) {
        int h = menu_wys(), my0 = fb_h - PASEK_H - h;
        menu_otwarte = 0;
        brudne(0, my0 - 10, MENU_W + 20, h + 20);
        brudne(0, fb_h - PASEK_H, 130, PASEK_H);
        if (w_prost(x, y, 0, my0, MENU_W, h)) {
            int iy = my0 + 6;
            for (int i = 0; i < MENU_N; i++) {
                int ih = menu[i].akcja == A_ODSTEP ? 9 : 28;
                if (y >= iy && y < iy + ih && menu[i].akcja != A_ODSTEP) { uruchom(menu[i].akcja); return; }
                iy += ih;
            }
            return;
        }
        if (y >= fb_h - PASEK_H && x < 126) return;
    }
    if (y >= fb_h - PASEK_H) { klik_pasek(x); return; }
    for (int k = zn - 1; k >= 0; k--) {
        int i = z[k];
        if (!widoczne(i)) continue;
        okno_t *o = &okna[i];
        if (!w_prost(x, y, o->x, o->y, ow(i), oh(i))) continue;
        okno_podnies(i);
        if (y < o->y + TB) {
            int bx = o->x + ow(i) - BR - 24;
            if (w_prost(x, y, bx, o->y + 4, 20, 18)) okno_zamknij(i);
            else if (i != O_KOMUNIKAT && w_prost(x, y, bx - 24, o->y + 4, 20, 18)) {
                o->zminim = 1; okno_brudne(i); brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);
            } else { ciagniete = i; cdx = x - o->x; cdy = y - o->y; }
        } else if (y >= o->y + TB && x >= o->x + BR) {
            int lx = x - o->x - BR, ly = y - o->y - TB;
            if (i == O_KALENDARZ) klik_kalendarz(lx, ly);
            else if (i == O_MUZYKA) klik_muzyka(lx, ly);
            else if (i == O_WWW) { klient_ciag = O_WWW; wwwg_klik(lx, ly); }
            else if (i == O_KALK) { klient_ciag = O_KALK; kalk_klik(lx, ly); }
            else if (i == O_KOMUNIKAT && w_prost(lx, ly, o->cw - 96, o->ch - 40, 84, 28)) okno_zamknij(i);
        }
        return;
    }
    for (int i = 0; i < IKONY_N; i++) {
        int ix, iy; ikona_poz(i, &ix, &iy);
        if (!w_prost(x, y, ix, iy, IK_W - 4, IK_H - 4)) continue;
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

void gui_mysz(int dx, int dy, int przyciski) {
    if (dx > 6 || dx < -6) dx *= 2;
    if (dy > 6 || dy < -6) dy *= 2;
    brudne(mx, my, 12, 19);
    mx += dx; my += dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > fb_w - 1) mx = fb_w - 1;
    if (my > fb_h - 1) my = fb_h - 1;
    brudne(mx, my, 12, 19);
    if (menu_otwarte) brudne(0, fb_h - PASEK_H - menu_wys(), MENU_W, menu_wys());

    if (ciagniete >= 0 && (przyciski & 1) && (dx || dy)) {
        okno_t *o = &okna[ciagniete];
        okno_brudne(ciagniete);
        o->x = mx - cdx; o->y = my - cdy;
        if (o->x < 60 - ow(ciagniete)) o->x = 60 - ow(ciagniete);
        if (o->x > fb_w - 60) o->x = fb_w - 60;
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
    przyc = przyciski;
}

void gui_klawisz(int k) {
    if (menu_otwarte) {
        if (k == KEY_ESC) { menu_otwarte = 0; brudne_wszystko(); }
        return;
    }
    int a = okno_aktywne();
    if (a < 0) return;
    if (a == O_WWW) { wwwg_klawisz(k); return; }
    if (a == O_KALK && k != KEY_ESC) { kalk_klawisz(k); return; }
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
                ost_min = dzis.min;
                liturgia(dzis.rok, dzis.mies, dzis.dzien, &dzis_lit);
                brudne(0, fb_h - PASEK_H, fb_w, PASEK_H);
                brudne(fb_w / 2, 0, fb_w / 2, 56);
            }
        }
    }
    terminal_sprawdz(t);
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
    int obszar_h = fb_h - PASEK_H;
    okna[O_TERMINAL].x = fb_w >= 1024 ? 196 : fb_w - ow(O_TERMINAL) - 8;
    okna[O_TERMINAL].y = obszar_h > 560 ? 70 : 8;
    okna[O_KALENDARZ].x = fb_w - ow(O_KALENDARZ) - 24; okna[O_KALENDARZ].y = 64;
    okna[O_ZEGAR].x = fb_w - ow(O_ZEGAR) - 60;         okna[O_ZEGAR].y = 90;
    okna[O_MUZYKA].x = (fb_w - ow(O_MUZYKA)) / 2 + 60; okna[O_MUZYKA].y = 120;
    okna[O_INFO].x = (fb_w - ow(O_INFO)) / 2;          okna[O_INFO].y = 150;
    okna[O_WWW].cw = fb_w >= 1000 ? 860 : fb_w - 40;
    okna[O_WWW].ch = obszar_h - TB - BR - 20;
    okna[O_WWW].x = (fb_w - ow(O_WWW)) / 2 + (fb_w >= 1000 ? 50 : 0);
    okna[O_WWW].y = 8;
    wwwg_init(okna[O_WWW].cw, okna[O_WWW].ch);
    okna[O_KALK].cw = kalk_szer(); okna[O_KALK].ch = kalk_wys();
    okna[O_KALK].x = fb_w - ow(O_KALK) - 40; okna[O_KALK].y = obszar_h - oh(O_KALK) > 30 ? 30 : 0;
    kalk_init(okna[O_KALK].cw, okna[O_KALK].ch);
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
    fb_gradient(0, 0, fb_w, fb_h, 0x24305E, 0x0A1024);
    int cx = fb_w / 2, cy = fb_h / 2 - 90;
    fb_prost(cx - 14, cy - 110, 28, 200, 0xF2C230);
    fb_prost(cx - 70, cy - 60, 140, 26, 0xF2C230);
    tekst_srodek(0, fb_w, cy + 120, "AmenOS", 0xFFFFFF, 4);
    char t[64]; ksprintf(t, sizeof t, "system operacyjny dla parafii · wersja %s", AMENOS_WERSJA);
    tekst_srodek(0, fb_w, cy + 196, t, 0x8CC8F4, 1);
    tekst_srodek(0, fb_w, fb_h - 60, "Niech będzie pochwalony Jezus Chrystus", 0xF2C230, 1);
    fb_wyslij(0, 0, fb_w, fb_h);
    czekaj_ms(2500);
    for (int i = 0; i < 64; i++) ps2_obsluz();
    gui_aktywne = 1;
    brudne_wszystko();
}
