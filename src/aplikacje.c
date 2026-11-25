/* AmenOS - programy graficzne: tablica pieśni, modlitewnik z różańcem, spowiedź, Komunia święta,
 * ogłoszenia parafialne, porządek Mszy i menedżer zadań.
 *
 * Każdy program to osobne okno ze swoim stanem. Nic tu nie czeka w pętli - programy reagują na
 * kliknięcia, klawisze i "tiki" zegara, więc wszystkie mogą działać jednocześnie. */
#include "kernel.h"
#include "apki.h"

#define TEKST   0x1B1F2A
#define SZARY_T 0x6B7280

static u32 los_stan = 20261123;
static u32 los(void) { los_stan = los_stan * 1103515245u + 12345u; return (los_stan >> 16) & 0x7FFF; }
static int miga(void) { return (int)((zegar_ms() / 500) & 1); }
static int w_prost(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

static void btn(int x, int y, int w, int h, const char *t, u32 tlo, u32 tk, int f) {
    fb_zaokr(x, y, w, h, 8, tlo, 256);
    if (tlo == 0xFFFFFF) fb_zaokr_ramka(x, y, w, h, 8, 0x000000, 40);
    fb_napis(x + (w - fb_napis_szer(t, f)) / 2, y + (h - fb_napis_wys(f)) / 2, t, tk, f);
}

/* wiersze wyśrodkowane, zawijane po słowach; kolor FB_PRZEZR = tylko policz */
static int zawin_srodek(int x, int w, int y, const char *s, u32 c, int f, int odstep) {
    int linie = 0;
    while (*s) {
        const char *p = s, *ost = 0;
        int sz = 0, sz_ost = 0;
        while (*p && *p != '\n') {
            const char *q = p;
            u32 cp = k_utf8_next(&q);
            int zw = fb_znak_szer(cp, f);
            if (sz + zw > w && p > s) break;
            if (cp == ' ') { ost = p; sz_ost = sz; }
            sz += zw; p = q;
        }
        const char *kon = p;
        int szk = sz;
        if (*p && *p != '\n' && ost) { kon = ost; szk = sz_ost; }
        if (c != FB_PRZEZR) fb_napis_n(x + (w - szk) / 2, y + linie * odstep, s, (int)(kon - s), c, f);
        linie++;
        s = kon;
        if (*s == '\n') s++;
        else while (*s == ' ') s++;
    }
    return linie;
}

/* ---------- pole tekstowe ---------- */

static int pole_klawisz(char *t, int rozm, int k) {
    int n = (int)k_strlen(t);
    if (k == KEY_BKSP) {
        if (!n) return 0;
        n--;
        while (n > 0 && ((u8)t[n] & 0xC0) == 0x80) n--;
        t[n] = 0;
        return 1;
    }
    if (k >= 32 && k < 0x1000 && k != 127) {
        char b[4];
        int l = k_utf8_put((u32)k, b);
        if (n + l >= rozm) return 0;
        memcpy(t + n, b, (u32)l);
        t[n + l] = 0;
        return 1;
    }
    return 0;
}

static void pole_rysuj(int x, int y, int w, int h, const char *t, const char *podp, int fokus) {
    int r = h / 2 > 12 ? 12 : h / 2;
    fb_zaokr(x, y, w, h, r, 0xFFFFFF, 256);
    if (fokus) fb_zaokr_ramka(x, y, w, h, r, gui_akcent(), 256);
    else fb_zaokr_ramka(x, y, w, h, r, 0x000000, 50);
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    fb_klip_zawez(x + 8, y, w - 16, h);
    const char *a = t;
    while (*a && fb_napis_szer(a, F_TEKST) > w - 32) k_utf8_next(&a);
    int ty = y + (h - fb_napis_wys(F_TEKST)) / 2;
    if (!*t && podp) { char p[200]; fb_napis_przytnij(p, sizeof p, podp, w - 32, F_TEKST); fb_napis(x + 14, ty, p, 0x9AA0AA, F_TEKST); }
    else fb_napis(x + 14, ty, a, TEKST, F_TEKST);
    if (fokus && !miga()) fb_prost(x + 14 + (*t ? fb_napis_szer(a, F_TEKST) : 0) + 1, ty + 1, 2, fb_napis_wys(F_TEKST) - 2, gui_akcent());
    fb_klip(kx, ky, kw, kh);
}

static int dzisiejszy_kolor(dzien_lit_t *l) {
    czas_t c; rtc_czytaj(&c);
    liturgia(c.rok, c.mies, c.dzien, l);
    return l->kolor;
}

/* ====================================================================== */
/*  Tablica pieśni                                                         */
/* ====================================================================== */

#define PI_W 520
#define PI_H 600
static int pi_edyt = -1, pi_miga;
static const char *pi_status = "Kliknij rząd i wpisz numer pieśni z klawiatury.";

static void pi_rysuj(int x, int y) {
    dzien_lit_t l;
    int k = dzisiejszy_kolor(&l);
    fb_prost(x, y, PI_W, PI_H, 0xF3F1EC);
    fb_prost(x, y, PI_W, 52, gui_kolor_lit(k));
    char t[120];
    fb_napis_przytnij(t, sizeof t, l.nazwa, PI_W - 40, F_TEKST_B);
    fb_napis_srodek(x, PI_W, y + 16, t, k == KOL_BIALY ? 0x5A4A20 : 0xFFFFFF, F_TEKST_B);
    /* drewniana tablica */
    fb_cien(x + 16, y + 68, PI_W - 32, 428, 14, 14, 90, 5);
    fb_zaokr_gradient(x + 16, y + 68, PI_W - 32, 428, 14, 0x9A6230, 0x5E3616);
    fb_zaokr(x + 28, y + 80, PI_W - 56, 404, 8, 0x241E1B, 256);
    fb_kolo_aa(x + 40, y + 92, 3, 0xC9A55A, 256);  fb_kolo_aa(x + PI_W - 40, y + 92, 3, 0xC9A55A, 256);
    fb_kolo_aa(x + 40, y + 472, 3, 0xC9A55A, 256); fb_kolo_aa(x + PI_W - 40, y + 472, 3, 0xC9A55A, 256);
    for (int i = 0; i < PIESNI_N; i++) {
        int ry = y + 84 + i * 66;
        if (i == pi_edyt) fb_zaokr(x + 34, ry, PI_W - 68, 62, 6, 0x3D332C, 256);
        fb_napis(x + 52, ry + 22, piesni_etyk[i], 0xE8C77A, F_TEKST_B);
        int n = (int)k_strlen(piesni[i]);
        int miejsca = (i == pi_edyt && n < 4) ? n + 1 : n;
        int cx = x + PI_W - 50 - miejsca * 46;
        for (int d = 0; d < n; d++) {
            fb_zaokr(cx + d * 46, ry + 4, 40, 54, 5, 0xF7F2E6, 256);
            char z[2] = { piesni[i][d], 0 };
            fb_napis(cx + d * 46 + (40 - fb_napis_szer(z, F_WIELKI)) / 2, ry + 4 + (54 - fb_napis_wys(F_WIELKI)) / 2, z, 0x2A1F14, F_WIELKI);
        }
        if (miejsca > n && !miga()) fb_zaokr_ramka(cx + n * 46, ry + 4, 40, 54, 5, 0xE8C77A, 256);
        if (!n && i != pi_edyt) {
            const char *pusto = "· · ·";
            fb_napis(x + PI_W - 52 - fb_napis_szer(pusto, F_SREDNI), ry + 20, pusto, 0x6E5E50, F_SREDNI);
        }
    }
    btn(x + 16, y + 512, 140, 38, "Wyczyść", 0xFFFFFF, TEKST, F_TEKST_B);
    btn(x + 166, y + 512, 140, 38, "Losuj numery", 0xFFFFFF, TEKST, F_TEKST_B);
    btn(x + PI_W - 156, y + 512, 140, 38, pi_edyt >= 0 ? "Gotowe" : "Edytuj", gui_akcent(), 0xFFFFFF, F_TEKST_B);
    fb_napis_zawin(x + 18, y + 562, PI_W - 36, pi_status, SZARY_T, F_MALY, 16);
}

static void pi_klik(int lx, int ly) {
    if (ly >= 84 && ly < 84 + PIESNI_N * 66 && lx >= 34 && lx < PI_W - 34) {
        int i = (ly - 84) / 66;
        pi_edyt = pi_edyt == i ? -1 : i;
        pi_status = pi_edyt >= 0 ? "Wpisz numer (do 4 cyfr). Enter - następny rząd, Esc - koniec." : "Zapisano. Organista już kartkuje śpiewnik.";
    } else if (w_prost(lx, ly, 16, 512, 140, 38)) {
        memset(piesni, 0, sizeof piesni); pi_edyt = -1;
        pi_status = "Tablica czysta. Organista może grać, co mu w duszy gra.";
    } else if (w_prost(lx, ly, 166, 512, 140, 38)) {
        los_stan ^= zegar_ms();
        for (int i = 0; i < PIESNI_N; i++) {
            if (i == PIESNI_N - 1) { piesni[i][0] = 0; break; }
            ksprintf(piesni[i], sizeof piesni[i], "%d", 1 + (int)(los() % 999));
        }
        pi_edyt = -1;
        pi_status = "Duch wieje, kędy chce (J 3,8) - numery wylosowane. Organista ma dziś niespodziankę!";
        dzw_efekt(E_AKORD);
    } else if (w_prost(lx, ly, PI_W - 156, 512, 140, 38)) {
        pi_edyt = pi_edyt >= 0 ? -1 : 0;
        pi_status = pi_edyt >= 0 ? "Wpisz numer (do 4 cyfr). Enter - następny rząd, Esc - koniec." : "Zapisano. Organista już kartkuje śpiewnik.";
    } else return;
    gui_apka_cala(AP_PIESNI);
}

static int pi_klawisz(int k) {
    if (pi_edyt < 0) {
        if (k == KEY_ENTER || (k >= '0' && k <= '9')) { pi_edyt = 0; if (k == KEY_ENTER) { gui_apka_cala(AP_PIESNI); return 1; } }
        else return 0;
    }
    char *p = piesni[pi_edyt];
    int n = (int)k_strlen(p);
    if (k >= '0' && k <= '9') { if (n < 4) { p[n] = (char)k; p[n + 1] = 0; } }
    else if (k == KEY_BKSP) { if (n) p[n - 1] = 0; }
    else if (k == KEY_ENTER || k == KEY_DOWN) { pi_edyt = pi_edyt + 1 < PIESNI_N ? pi_edyt + 1 : -1; }
    else if (k == KEY_UP) { if (pi_edyt > 0) pi_edyt--; }
    else if (k == KEY_ESC) { pi_edyt = -1; pi_status = "Zapisano. Organista już kartkuje śpiewnik."; }
    else return 1;
    gui_apka_cala(AP_PIESNI);
    return 1;
}

static void pi_tik(u32 t) {
    (void)t;
    if (pi_edyt >= 0 && miga() != pi_miga) { pi_miga = miga(); gui_apka_brudne(AP_PIESNI, 28, 80, PI_W - 56, 404); }
}
static void pi_zamk(void) { pi_edyt = -1; }

/* ====================================================================== */
/*  Modlitewnik i różaniec                                                 */
/* ====================================================================== */

#define MO_W 820
#define MO_H 560
#define MO_SIDE 230
static int mo_wybr, mo_licznik[16], ro_paciorek, ro_dzies, ro_razem;
static const char *mo_status = "";

static const char *TAJ_NAZWA[4] = { "radosne", "światła", "bolesne", "chwalebne" };
static const char *TAJ[4][5] = {
    { "Zwiastowanie Najświętszej Maryi Pannie", "Nawiedzenie świętej Elżbiety", "Narodzenie Pana Jezusa",
      "Ofiarowanie Pana Jezusa w świątyni", "Odnalezienie Pana Jezusa w świątyni" },
    { "Chrzest Pana Jezusa w Jordanie", "Objawienie się Pana Jezusa w Kanie Galilejskiej",
      "Głoszenie Królestwa Bożego i wzywanie do nawrócenia", "Przemienienie na górze Tabor", "Ustanowienie Eucharystii" },
    { "Modlitwa Pana Jezusa w Ogrójcu", "Biczowanie Pana Jezusa", "Cierniem ukoronowanie",
      "Dźwiganie krzyża", "Śmierć Pana Jezusa na krzyżu" },
    { "Zmartwychwstanie Pana Jezusa", "Wniebowstąpienie Pana Jezusa", "Zesłanie Ducha Świętego",
      "Wniebowzięcie Najświętszej Maryi Panny", "Ukoronowanie Maryi na Królową Nieba i Ziemi" },
};
static int taj_dnia(void) {                      /* Nd chwalebne, Pn radosne, Wt bolesne, Śr chwalebne, Cz światła, Pt bolesne, So radosne */
    static const int t[7] = { 3, 0, 2, 3, 1, 2, 0 };
    czas_t c; rtc_czytaj(&c);
    return t[dzien_tygodnia(dni_od_epoki(c.rok, c.mies, c.dzien))];
}

static const char *ODMOWIONE[] = { "Zapisane w niebie ✓", "Anioł Stróż przybił pieczątkę.", "Niebo potwierdza odbiór.",
                                   "Święty Piotr dopisał to do zeszytu.", "Modlitwa doręczona. Bez opłat i bez kolejki." };

static void mo_rysuj(int x, int y) {
    /* pasek boczny */
    fb_prost(x, y, MO_SIDE, MO_H, 0xF1EFE9);
    fb_prost(x + MO_SIDE - 1, y, 1, MO_H, 0xE2DDD0);
    fb_napis(x + 22, y + 20, "Modlitewnik", TEKST, F_SREDNI);
    for (int i = 0; i <= modlitwy_n; i++) {
        int iy = y + 62 + i * 38;
        if (i == mo_wybr) fb_zaokr(x + 10, iy, MO_SIDE - 20, 34, 8, fb_mieszaj(0xFFFFFF, gui_akcent(), 46), 256);
        char t[64];
        if (i == modlitwy_n) k_strncpy(t, "Różaniec", sizeof t);
        else fb_napis_przytnij(t, sizeof t, modlitwy[i].tytul, MO_SIDE - 48, F_TEKST);
        fb_napis(x + 24, iy + 8, t, i == mo_wybr ? TEKST : 0x3D4250, i == modlitwy_n || i == mo_wybr ? F_TEKST_B : F_TEKST);
    }
    fb_kolo_aa(x + 26, y + MO_H - 26, 4, 0x22C55E, 256);
    fb_napis(x + 36, y + MO_H - 35, "Anioł Stróż: online", SZARY_T, F_MALY);

    /* strona */
    int px = x + MO_SIDE, pw = MO_W - MO_SIDE;
    fb_prost(px, y, pw, MO_H, 0xFBF7EC);
    if (mo_wybr < modlitwy_n) {
        const modlitwa_t *m = &modlitwy[mo_wybr];
        fb_napis_srodek(px, pw, y + 26, m->tytul, 0x6B3A12, F_DUZY);
        int sz = fb_napis_szer(m->tytul, F_DUZY) / 2 + 20;
        fb_prost(px + pw / 2 - sz - 60, y + 46, 60, 1, 0xD8C49A);
        fb_prost(px + pw / 2 + sz, y + 46, 60, 1, 0xD8C49A);
        int f = F_SREDNI, od = 28, razem = 0, n = 0;
        while (m->linie[n]) n++;
        for (int i = 0; i < n; i++) razem += m->linie[i][0] ? zawin_srodek(0, pw - 80, 0, m->linie[i], FB_PRZEZR, f, od) : 1;
        if (razem * od > 380) { f = F_WWW; od = 23; razem = 0; for (int i = 0; i < n; i++) razem += m->linie[i][0] ? zawin_srodek(0, pw - 80, 0, m->linie[i], FB_PRZEZR, f, od) : 1; }
        int ly = y + 84 + (400 - razem * od) / 2;
        for (int i = 0; i < n; i++)
            ly += (m->linie[i][0] ? zawin_srodek(px + 40, pw - 80, ly, m->linie[i], 0x2E2618, f, od) : 1) * od;
        btn(px + pw / 2 - 90, y + MO_H - 62, 180, 40, "Odmówione ✓", gui_akcent(), 0xFFFFFF, F_TEKST_B);
        char t[64];
        ksprintf(t, sizeof t, "Odmówiono dziś: %d", mo_licznik[mo_wybr]);
        fb_napis(px + pw - 20 - fb_napis_szer(t, F_MALY), y + MO_H - 50, t, SZARY_T, F_MALY);
        if (mo_status[0]) fb_napis(px + 20, y + MO_H - 50, mo_status, 0x8A6A20, F_MALY);
    } else {
        int tj = taj_dnia();
        fb_napis_srodek(px, pw, y + 26, "Różaniec", 0x6B3A12, F_DUZY);
        char t[120];
        ksprintf(t, sizeof t, "Dziś tajemnice %s", TAJ_NAZWA[tj]);
        fb_napis_srodek(px, pw, y + 66, t, SZARY_T, F_TEKST);
        ksprintf(t, sizeof t, "%d. %s", ro_dzies + 1, TAJ[tj][ro_dzies]);
        zawin_srodek(px + 30, pw - 60, y + 112, t, gui_akcent(), F_SREDNI, 26);
        /* paciorki: Ojcze nasz i dziesięć Zdrowaś Maryjo */
        int bx = px + (pw - 470) / 2, by = y + 250;
        fb_linia_aa(bx, by, bx + 470, by, 2, 0xB59A6A);
        fb_kolo_aa(bx + 10, by, 15, 0x7A4A22, 256);
        fb_kolo_aa(bx + 6, by - 5, 4, 0xC08A5A, 256);
        fb_zaokr(bx + 7, by + 18, 6, 34, 2, 0xC9A43A, 256);
        fb_zaokr(bx - 3, by + 26, 26, 6, 2, 0xC9A43A, 256);
        for (int i = 0; i < 10; i++) {
            int cx = bx + 66 + i * 42;
            int zrob = i < ro_paciorek;
            fb_kolo_aa(cx, by, 12, zrob ? gui_akcent() : 0xFFFFFF, 256);
            fb_okrag_aa(cx, by, 12, 1, zrob ? gui_akcent() : 0xB59A6A, 256);
            if (zrob) fb_kolo_aa(cx - 3, by - 4, 3, 0xFFFFFF, 110);
            if (i == ro_paciorek) fb_okrag_aa(cx, by, 17, 2, gui_akcent(), miga() ? 120 : 256);
        }
        btn(px + pw / 2 - 140, y + 316, 280, 50, "Zdrowaś Maryjo  +1", gui_akcent(), 0xFFFFFF, F_SREDNI);
        btn(px + pw / 2 - 60, y + 378, 120, 34, "Od nowa", 0xFFFFFF, TEKST, F_TEKST_B);
        ksprintf(t, sizeof t, "Dziesiątka %d z 5  ·  Zdrowaś Maryjo razem: %d", ro_dzies + 1, ro_razem);
        fb_napis_srodek(px, pw, y + 432, t, SZARY_T, F_TEKST);
        if (mo_status[0]) zawin_srodek(px + 30, pw - 60, y + 462, mo_status, 0x8A6A20, F_TEKST_B, 20);
        fb_napis_srodek(px, pw, y + MO_H - 34, "Spacja albo Enter - kolejne Zdrowaś Maryjo", 0xA09A8C, F_MALY);
    }
}

static void ro_plus(void) {
    ro_razem++;
    mo_status = "";
    dzw_efekt(E_PACIOREK);
    if (++ro_paciorek == 10) {
        ro_paciorek = 0;
        if (ro_dzies == 4) { ro_dzies = 0; mo_status = "Cała część różańca! Babcia byłaby dumna."; }
        else { ro_dzies++; mo_status = "Dziesiątka zaliczona! Anioł Stróż przybija piątkę."; }
        dzw_nuta(INS_DZWONEK, 1567980, 0, 70);
    }
    gui_apka_cala(AP_MODLITWY);
}

static void mo_klik(int lx, int ly) {
    if (lx < MO_SIDE) {
        int i = (ly - 62) / 38;
        if (ly >= 62 && i >= 0 && i <= modlitwy_n) { mo_wybr = i; mo_status = ""; gui_apka_cala(AP_MODLITWY); }
        return;
    }
    int px = MO_SIDE, pw = MO_W - MO_SIDE;
    if (mo_wybr < modlitwy_n) {
        if (w_prost(lx, ly, px + pw / 2 - 90, MO_H - 62, 180, 40)) {
            mo_licznik[mo_wybr]++;
            mo_status = ODMOWIONE[(mo_licznik[mo_wybr] - 1) % 5];
            gui_apka_cala(AP_MODLITWY);
        }
    } else {
        if (w_prost(lx, ly, px + pw / 2 - 140, 316, 280, 50)) ro_plus();
        else if (w_prost(lx, ly, px + pw / 2 - 60, 378, 120, 34)) { ro_paciorek = ro_dzies = 0; mo_status = "Zaczynamy od nowa. W imię Ojca i Syna, i Ducha Świętego."; gui_apka_cala(AP_MODLITWY); }
    }
}

static int mo_klawisz(int k) {
    if (k == KEY_UP && mo_wybr > 0) mo_wybr--;
    else if (k == KEY_DOWN && mo_wybr < modlitwy_n) mo_wybr++;
    else if ((k == ' ' || k == KEY_ENTER) && mo_wybr == modlitwy_n) { ro_plus(); return 1; }
    else return 0;
    mo_status = "";
    gui_apka_cala(AP_MODLITWY);
    return 1;
}

static int mo_miga;
static void mo_tik(u32 t) {
    (void)t;
    if (mo_wybr == modlitwy_n && miga() != mo_miga) { mo_miga = miga(); gui_apka_brudne(AP_MODLITWY, MO_SIDE, 220, MO_W - MO_SIDE, 60); }
}

/* ====================================================================== */
/*  Spowiedź - rozmowa jak w komunikatorze; na końcu niszczarka            */
/* ====================================================================== */

#define SP_W 560
#define SP_H 640
#define SP_DOL 112
#define SPM 48
#define SP_MAXW 380

/* Wszystko, co wierny napisze, jest tylko tutaj - i zostaje wymazane po zakończeniu. */
static struct {
    int etap;                        /* 0 wstęp, 1-7 rozmowa, 8 koniec, 9 niszczarka, 10 wymazano */
    char t[SPM][640]; u8 kto[SPM];   /* 0 kapłan, 1 wierny, 2 formuła rozgrzeszenia */
    int n;
    char kol[4][640]; u8 kol_kto[4]; int kn; u32 nast;
    char wej[300];
    int grzechow, zachecono, tem[11], kolej[11], ile_tem;
    u32 niszcz_t0;
    int miga;
} sp;
static const char *sp_podp, *sp_dom;

static void sp_dodaj(int kto, const char *s) {
    if (sp.n == SPM) {
        for (int i = 0; i < SPM - 1; i++) { memcpy(sp.t[i], sp.t[i + 1], sizeof sp.t[0]); sp.kto[i] = sp.kto[i + 1]; }
        sp.n--;
    }
    k_strncpy(sp.t[sp.n], s, sizeof sp.t[0]);
    sp.kto[sp.n++] = (u8)kto;
}

static u32 sp_opoznienie(const char *s) { u32 d = 900 + k_utf8_len(s) * 14; return d > 3800 ? 3800 : d; }

static void sp_kaplan(const char *s, int kto) {
    if (sp.kn == 4) return;
    if (!sp.kn) sp.nast = zegar_ms() + sp_opoznienie(s);
    k_strncpy(sp.kol[sp.kn], s, sizeof sp.kol[0]);
    sp.kol_kto[sp.kn++] = (u8)kto;
}

static void sp_ustaw_podp(void) {
    static const char *P[8] = { 0, "Zacznij od pozdrowienia", "Kiedy była ostatnia spowiedź? Czy pokuta została odprawiona?",
                                "Wpisz grzech i Enter - każdy w osobnej wiadomości", "Odmów akt żalu", "Odpowiedz", "Odpowiedz", "Odpowiedz" };
    static const char *D[8] = { 0, "Niech będzie pochwalony Jezus Chrystus.", "Ostatnia spowiedź: miesiąc temu. Pokuta odprawiona.",
                                "Więcej grzechów nie pamiętam.", 0, "Amen.", "Bo Jego miłosierdzie trwa na wieki.", "Bóg zapłać." };
    sp_podp = sp.etap >= 1 && sp.etap <= 7 ? P[sp.etap] : 0;
    sp_dom = sp.etap >= 1 && sp.etap <= 7 ? (sp.etap == 4 ? AKT_ZALU : D[sp.etap]) : 0;
}

static void sp_wymaz(void) {
    volatile u8 *p = (volatile u8 *)&sp;
    for (u32 i = 0; i < sizeof sp; i++) p[i] = 0;
}

static void sp_odpowiedz(void) {
    if (sp.kn || sp.etap < 1 || sp.etap > 7) return;
    char tekst[300], male[320];
    k_strncpy(tekst, sp.wej[0] ? sp.wej : (sp_dom ? sp_dom : ""), sizeof tekst);
    k_fold_pl(male, tekst, sizeof male);
    switch (sp.etap) {
    case 1:
        sp_dodaj(1, tekst);
        sp_kaplan("Na wieki wieków. Amen. W imię Ojca i Syna, i Ducha Świętego. Bóg, który oświeca każde serce, "
                  "niech ci da prawdziwe poznanie twoich grzechów i swojego miłosierdzia.", 0);
        sp.etap = 2;
        break;
    case 2:
        sp_dodaj(1, tekst);
        sp_kaplan("Słucham. Wyznaj swoje grzechy - możesz pisać po kolei, każdy w osobnej wiadomości.", 0);
        sp.etap = 3;
        break;
    case 3:
        if (!sp.wej[0] && !sp.grzechow && !sp.zachecono) {
            sp.zachecono = 1;
            sp_kaplan("Nie bój się. Bóg zna twoje serce i czeka na ciebie z miłością. Co chcesz Mu powiedzieć?", 0);
            break;
        }
        if (!sp.wej[0] || zawiera(male, "nie pamietam") || zawiera(male, "prosze o pokute")) {
            sp_dodaj(1, sp.wej[0] ? tekst : "Więcej grzechów nie pamiętam. Za wszystkie serdecznie żałuję i proszę o pokutę i rozgrzeszenie.");
            char nauka[640], pokuta[400];
            ksprintf(nauka, sizeof nauka, "Dziękuję za szczere wyznanie. Bóg cieszy się z każdego, kto do Niego wraca. ");
            for (int i = 0; i < sp.ile_tem && i < 2; i++) {
                int p = (int)k_strlen(nauka);
                ksprintf(nauka + p, sizeof nauka - (u32)p, "%s ", sp_tematy[sp.kolej[i]].nauka);
            }
            int p = (int)k_strlen(nauka);
            ksprintf(nauka + p, sizeof nauka - (u32)p, "Pamiętaj, że Boże miłosierdzie jest większe niż każdy grzech.");
            const char *mod = sp.grzechow <= 3 ? "jedną dziesiątkę różańca" : sp.grzechow <= 7 ? "Koronkę do Miłosierdzia Bożego" : "jedną część różańca";
            if (sp.ile_tem) ksprintf(pokuta, sizeof pokuta, "Za pokutę odmów %s. %s", mod, sp_tematy[sp.kolej[0]].pokuta);
            else ksprintf(pokuta, sizeof pokuta, "Za pokutę odmów %s.", mod);
            sp_kaplan(nauka, 0);
            sp_kaplan(pokuta, 0);
            sp_kaplan("Czy żałujesz za swoje grzechy? Odmów teraz akt żalu.", 0);
            memset(nauka, 0, sizeof nauka); memset(pokuta, 0, sizeof pokuta);
            sp.etap = 4;
        } else {
            sp_dodaj(1, tekst);
            sp.grzechow++;
            for (int t = 0; t < 11; t++)
                for (int k = 0; k < 12 && sp_tematy[t].klucze[k]; k++)
                    if (zawiera(male, sp_tematy[t].klucze[k])) {
                        if (!sp.tem[t]) { sp.tem[t] = 1; sp.kolej[sp.ile_tem++] = t; }
                        break;
                    }
        }
        break;
    case 4: {
        sp_dodaj(1, tekst);
        char f[640];                                  /* formuła: krzyż w miejsce znaku ramki z wersji tekstowej */
        k_strncpy(f, ROZGRZESZENIE, sizeof f);
        int n = (int)k_strlen(f);
        if (n >= 4 && !k_strcmp(f + n - 4, " ┼")) k_strncpy(f + n - 4, " ✝", 8);
        sp_kaplan(f, 2);
        sp.etap = 5;
        break;
    }
    case 5: sp_dodaj(1, tekst); sp_kaplan("Chwalcie Pana, bo jest dobry.", 0); sp.etap = 6; break;
    case 6: sp_dodaj(1, tekst); sp_kaplan("Pan odpuścił tobie grzechy. Idź w pokoju.", 0); sp.etap = 7; break;
    case 7: sp_dodaj(1, tekst); sp.etap = 8; break;
    }
    memset(sp.wej, 0, sizeof sp.wej);
    memset(tekst, 0, sizeof tekst); memset(male, 0, sizeof male);
    sp_ustaw_podp();
    gui_apka_cala(AP_SPOWIEDZ);
}

static int sp_wys_dymka(const char *s, int *szer) {
    int lin = fb_napis_zawin(0, 0, SP_MAXW - 28, s, FB_PRZEZR, F_TEKST, 19);
    *szer = lin == 1 ? fb_napis_szer(s, F_TEKST) + 28 : SP_MAXW;
    return lin * 19 + 20;
}

static void sp_rysuj(int x, int y) {
    u32 ak = gui_akcent();
    /* nagłówek */
    fb_prost(x, y, SP_W, 64, 0xFFFFFF);
    fb_prost(x, y + 63, SP_W, 1, 0xE5E7EB);
    fb_kolo_aa(x + 38, y + 32, 22, 0xEDE9FE, 256);
    fb_ikona_rys(x + 26, y + 20, 7, 24);
    fb_napis(x + 72, y + 13, "Spowiednik", TEKST, F_TEKST_B);
    fb_kolo_aa(x + 77, y + 43, 4, 0x22C55E, 256);
    fb_napis(x + 87, y + 34, "zawsze dostępny · rozmowa zniknie po zakończeniu", SZARY_T, F_MALY);
    int gy = y + 64, dy = y + SP_H - SP_DOL;
    fb_prost(x, gy, SP_W, dy - gy, 0xF4F5F9);

    if (sp.etap == 0 || sp.etap == 10) {
        int cy = gy + 70;
        fb_ikona_rys(x + SP_W / 2 - 24, cy, sp.etap ? 11 : 7, 48);
        if (sp.etap == 0) {
            fb_napis_srodek(x, SP_W, cy + 64, "Spowiedź święta", TEKST, F_DUZY);
            zawin_srodek(x + 50, SP_W - 100, cy + 112, "Rozmowa jest prywatna: AmenOS nie ma dysku, a po zakończeniu cała rozmowa "
                         "trafi do niszczarki i zostanie wymazana z pamięci.", SZARY_T, F_TEKST, 21);
            zawin_srodek(x + 50, SP_W - 100, cy + 190, "Kolejka do konfesjonału: 0 osób. Dziś bez czekania!", 0x8A6A20, F_TEKST_B, 21);
            btn(x + SP_W / 2 - 90, cy + 240, 180, 44, "Rozpocznij", ak, 0xFFFFFF, F_TEKST_B);
        } else {
            fb_napis_srodek(x, SP_W, cy + 64, "Szczęść Boże!", 0x8A6A20, F_DUZY);
            zawin_srodek(x + 50, SP_W - 100, cy + 112, "Rozmowa została zmielona w niszczarce i wymazana z pamięci. "
                         "Nikt jej nie przeczyta - nawet ten komputer.", SZARY_T, F_TEKST, 21);
            btn(x + SP_W / 2 - 90, cy + 190, 180, 44, "Nowa spowiedź", 0xFFFFFF, TEKST, F_TEKST_B);
        }
        fb_prost(x, dy, SP_W, SP_DOL, 0xFFFFFF);
        return;
    }

    /* dymki od dołu */
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    fb_klip_zawez(x, gy, SP_W, dy - gy);
    int przes = 0;
    if (sp.etap == 9) { int p = (int)(zegar_ms() - sp.niszcz_t0); if (p > 1800) p = 1800; przes = p * p / 5400; }
    int yb = dy - 14 + przes;
    if (sp.kn) {                                          /* "Spowiednik pisze..." */
        yb -= 38;
        fb_zaokr(x + 16, yb, 70, 36, 16, 0xFFFFFF, 256);
        fb_zaokr_ramka(x + 16, yb, 70, 36, 16, 0x000000, 24);
        int faza = (int)(zegar_ms() / 280) % 3;
        for (int i = 0; i < 3; i++) fb_kolo_aa(x + 36 + i * 15, yb + 18, 4, 0x9AA0AA, i == faza ? 256 : 110);
        yb -= 10;
    }
    for (int i = sp.n - 1; i >= 0 && yb > gy - 40; i--) {
        int w, h = sp_wys_dymka(sp.t[i], &w);
        yb -= h;
        int bx = sp.kto[i] == 1 ? x + SP_W - 16 - w : x + 16;
        u32 tlo = sp.kto[i] == 1 ? ak : sp.kto[i] == 2 ? 0xFFF4D6 : 0xFFFFFF;
        u32 tk = sp.kto[i] == 1 ? 0xFFFFFF : sp.kto[i] == 2 ? 0x5A4210 : TEKST;
        fb_zaokr(bx, yb, w, h, 16, tlo, 256);
        if (sp.kto[i] == 0) fb_zaokr_ramka(bx, yb, w, h, 16, 0x000000, 24);
        if (sp.kto[i] == 2) fb_zaokr_ramka(bx, yb, w, h, 16, 0xE2C26A, 256);
        fb_napis_zawin(bx + 14, yb + 9, SP_MAXW - 28, sp.t[i], tk, F_TEKST, 19);
        yb -= 10;
    }
    fb_klip(kx, ky, kw, kh);

    /* dół: odpowiedź */
    fb_prost(x, dy, SP_W, SP_DOL, 0xFFFFFF);
    fb_prost(x, dy, SP_W, 1, 0xE5E7EB);
    if (sp.etap >= 1 && sp.etap <= 7) {
        if (sp_dom && !sp.kn) {
            char c[200], d[220];
            ksprintf(d, sizeof d, "»  %s", sp_dom);
            fb_napis_przytnij(c, sizeof c, d, SP_W - 64, F_TEKST);
            int cw = fb_napis_szer(c, F_TEKST) + 28;
            fb_zaokr(x + 16, dy + 12, cw, 32, 16, fb_mieszaj(0xFFFFFF, ak, 26), 256);
            fb_zaokr_ramka(x + 16, dy + 12, cw, 32, 16, ak, 140);
            fb_napis(x + 30, dy + 19, c, fb_mieszaj(ak, 0x000000, 60), F_TEKST);
        }
        pole_rysuj(x + 16, dy + 56, SP_W - 84, 44, sp.wej, sp.kn ? "Spowiednik pisze…" : sp_podp, !sp.kn);
        int sx = x + SP_W - 40, sy = dy + 78;
        fb_kolo_aa(sx, sy, 21, sp.kn ? 0xC8CCD4 : ak, 256);
        fb_linia_aa(sx - 7, sy, sx + 7, sy, 2, 0xFFFFFF);
        fb_linia_aa(sx + 1, sy - 6, sx + 7, sy, 2, 0xFFFFFF);
        fb_linia_aa(sx + 1, sy + 6, sx + 7, sy, 2, 0xFFFFFF);
    } else if (sp.etap == 8) {
        fb_napis_srodek(x, SP_W, dy + 14, "Pan odpuścił ci grzechy. Rozmowa może zniknąć.", SZARY_T, F_MALY);
        btn(x + SP_W / 2 - 150, dy + 42, 300, 46, "Zakończ i zmiel w niszczarce", ak, 0xFFFFFF, F_TEKST_B);
    } else if (sp.etap == 9) {
        /* niszczarka: szczelina u góry dolnego paska, z dołu wypadają paski papieru */
        fb_zaokr(x + 60, dy - 18, SP_W - 120, 30, 8, 0x3A3F4B, 256);
        fb_prost(x + 80, dy - 6, SP_W - 160, 4, 0x15171C);
        int p = (int)(zegar_ms() - sp.niszcz_t0);
        int dl = p / 18 > 90 ? 90 : p / 18;
        for (int i = 0; i < 26; i++) {
            int sx = x + 90 + i * ((SP_W - 180) / 26);
            int l = dl - (int)((i * 7) % 23);
            if (l > 0) fb_prost(sx, dy + 12, 6, l, i % 3 == 0 ? fb_mieszaj(0xFFFFFF, ak, 120) : (i % 3 == 1 ? 0xFFFFFF : 0xE8EAEE));
        }
        fb_napis_srodek(x, SP_W, dy + SP_DOL - 22, "Mielenie… wrrrr…", SZARY_T, F_MALY);
    }
}

static void sp_start(void) {
    sp_wymaz();
    sp.etap = 1;
    sp_ustaw_podp();
    gui_apka_cala(AP_SPOWIEDZ);
}

static void sp_klik(int lx, int ly) {
    int gy = 64, dy = SP_H - SP_DOL;
    if (sp.etap == 0 && w_prost(lx, ly, SP_W / 2 - 90, gy + 70 + 240, 180, 44)) { sp_start(); return; }
    if (sp.etap == 10 && w_prost(lx, ly, SP_W / 2 - 90, gy + 70 + 190, 180, 44)) { sp.etap = 0; gui_apka_cala(AP_SPOWIEDZ); return; }
    if (sp.etap == 8 && w_prost(lx, ly, SP_W / 2 - 150, dy + 42, 300, 46)) { sp.etap = 9; sp.niszcz_t0 = zegar_ms(); dzw_efekt(E_NISZCZARKA); gui_apka_cala(AP_SPOWIEDZ); return; }
    if (sp.etap >= 1 && sp.etap <= 7 && !sp.kn) {
        if (ly >= dy + 8 && ly < dy + 48 && sp_dom) { memset(sp.wej, 0, sizeof sp.wej); sp_odpowiedz(); return; }
        if (w_prost(lx, ly, SP_W - 62, dy + 56, 46, 46)) sp_odpowiedz();
    }
}

static int sp_klawisz(int k) {
    if (sp.etap == 0 || sp.etap == 10) { if (k == KEY_ENTER) { if (sp.etap) { sp.etap = 0; gui_apka_cala(AP_SPOWIEDZ); } else sp_start(); return 1; } return 0; }
    if (sp.etap == 8) { if (k == KEY_ENTER) { sp.etap = 9; sp.niszcz_t0 = zegar_ms(); dzw_efekt(E_NISZCZARKA); gui_apka_cala(AP_SPOWIEDZ); return 1; } return 0; }
    if (sp.etap == 9) return 1;
    if (k == KEY_ESC) return 0;
    if (sp.kn) return 1;
    if (k == KEY_ENTER) { sp_odpowiedz(); return 1; }
    if (pole_klawisz(sp.wej, sizeof sp.wej, k)) gui_apka_brudne(AP_SPOWIEDZ, 0, SP_H - SP_DOL, SP_W, SP_DOL);
    return 1;
}

static void sp_tik(u32 t) {
    static u32 ost;
    if (sp.kn) {
        if ((int)(t - sp.nast) >= 0) {
            sp_dodaj(sp.kol_kto[0], sp.kol[0]);
            dzw_efekt(E_WIADOMOSC);
            for (int i = 0; i < sp.kn - 1; i++) { memcpy(sp.kol[i], sp.kol[i + 1], sizeof sp.kol[0]); sp.kol_kto[i] = sp.kol_kto[i + 1]; }
            memset(sp.kol[--sp.kn], 0, sizeof sp.kol[0]);
            if (sp.kn) sp.nast = t + sp_opoznienie(sp.kol[0]);
            gui_apka_cala(AP_SPOWIEDZ);
        } else if (t - ost > 280) { ost = t; gui_apka_brudne(AP_SPOWIEDZ, 0, SP_H - SP_DOL - 70, SP_W, SP_DOL + 70); }
    } else if (sp.etap == 9) {
        if (t - sp.niszcz_t0 > 2000) { sp_wymaz(); sp.etap = 10; gui_apka_cala(AP_SPOWIEDZ); }
        else if (t - ost > 40) { ost = t; gui_apka_cala(AP_SPOWIEDZ); }
    } else if (sp.etap >= 1 && sp.etap <= 7 && miga() != sp.miga) {
        sp.miga = miga();
        gui_apka_brudne(AP_SPOWIEDZ, 0, SP_H - SP_DOL + 50, SP_W, 56);
    }
}

static void sp_zamk(void) { sp_wymaz(); sp_podp = sp_dom = 0; }

/* ====================================================================== */
/*  Komunia święta                                                         */
/* ====================================================================== */

#define KO_W 560
#define KO_H 680
static int ko_etap, ko_seria, ko_faza, ko_przel;
static u32 ko_t0, ko_tp, ko_dzw_ms;

static void ko_gwiazdka(int cx, int cy, int a) {
    u32 c = fb_mieszaj(0x1C2550, 0xFFF4C8, a);
    fb_kolo_aa(cx, cy, 2, 0xFFF4C8, a);
    fb_linia_aa(cx - 6, cy, cx + 6, cy, 1, c);
    fb_linia_aa(cx, cy - 6, cx, cy + 6, 1, c);
}
static const short GW[7][2] = { { 90, 70 }, { 470, 96 }, { 120, 300 }, { 440, 330 }, { 60, 190 }, { 500, 220 }, { 280, 30 } };
static int gw_alfa(int i) { int f = (int)((zegar_ms() / 110 + (u32)i * 5) % 14); return (f < 7 ? f : 14 - f) * 35 + 8; }

static void ko_rysuj(int x, int y) {
    fb_gradient(x, y, KO_W, KO_H, 0x202A5E, 0x0C1028);
    int cx = x + KO_W / 2, cy = y + 196;
    fb_kolo_aa(cx, cy, 160, 0xF6C640, 16);
    fb_kolo_aa(cx, cy, 118, 0xF6C640, 22);
    for (int i = 0; i < 60; i += 2) {
        int dlugi = (i / 2) % 2 == 0, r1 = 76, r2 = dlugi ? 150 : 122;
        fb_linia_aa(cx + fb_sinus(i) * r1 / 1000, cy - fb_sinus(i + 15) * r1 / 1000,
                    cx + fb_sinus(i) * r2 / 1000, cy - fb_sinus(i + 15) * r2 / 1000, dlugi ? 3 : 2, dlugi ? 0xF6C640 : 0xC9963A);
    }
    fb_okrag_aa(cx, cy, 72, 8, 0xE2B13C, 256);
    fb_okrag_aa(cx, cy, 72, 2, 0xFFE08A, 256);
    fb_kolo_aa(cx, cy, 63, 0xFFFDF4, 256);
    fb_okrag_aa(cx, cy, 63, 2, 0xEFE4CC, 256);
    fb_linia_aa(cx, cy - 32, cx, cy + 32, 3, 0xEDE2C8);
    fb_linia_aa(cx - 22, cy - 10, cx + 22, cy - 10, 3, 0xEDE2C8);
    fb_zaokr_gradient(cx - 8, cy + 80, 16, 86, 6, 0xF6C640, 0xB8862A);
    fb_kolo_aa(cx, cy + 118, 11, 0xE2B13C, 256);
    fb_zaokr(cx - 48, cy + 160, 96, 16, 8, 0xD9A63A, 256);
    fb_zaokr(cx - 72, cy + 172, 144, 18, 9, 0xF0BE48, 256);
    for (int i = 0; i < 7; i++) ko_gwiazdka(x + GW[i][0], y + GW[i][1], gw_alfa(i));

    int ty = y + 420;
    if (ko_etap <= 1) {
        zawin_srodek(x + 30, KO_W - 60, ty + 10, "Oto Baranek Boży, który gładzi grzechy świata. Błogosławieni, którzy zostali wezwani na Jego ucztę.",
                     0xF6DE8E, F_SREDNI, 27);
        if (ko_etap == 0) {
            btn(x + KO_W / 2 - 190, ty + 118, 380, 46, "Panie, nie jestem godzien…", 0xFFFFFF, TEKST, F_TEKST_B);
            fb_napis_srodek(x, KO_W, ty + 176, "kliknij albo naciśnij Enter", 0x8C96C0, F_MALY);
        } else
            zawin_srodek(x + 40, KO_W - 80, ty + 108, "Panie, nie jestem godzien, abyś przyszedł do mnie, ale powiedz tylko słowo, a będzie uzdrowiona dusza moja.",
                         0xFFFFFF, F_TEKST, 21);
    } else if (ko_etap == 2) {
        fb_napis_srodek(x, KO_W, ty + 60, "Dzyń!  Dzyń!  Dzyń!", 0xF6DE8E, F_DUZY);
        fb_napis_srodek(x, KO_W, ty + 106, audio_jest ? "(dzwonki ministranckie - tym razem prawdziwe)" : "(dzwonki - głośnik komputera stara się, jak może)", 0x8C96C0, F_MALY);
    } else if (ko_etap <= 4) {
        fb_napis_srodek(x, KO_W, ty + 30, "Ciało Chrystusa.", 0xF6DE8E, F_DUZY);
        if (ko_etap == 3) {
            btn(x + KO_W / 2 - 90, ty + 108, 180, 46, "Amen", 0xFFFFFF, TEKST, F_SREDNI);
            fb_napis_srodek(x, KO_W, ty + 166, "kliknij albo naciśnij Enter", 0x8C96C0, F_MALY);
        } else fb_napis_srodek(x, KO_W, ty + 112, "Amen.", 0xFFFFFF, F_SREDNI);
    } else {
        fb_napis_srodek(x, KO_W, ty + 4, "Dziękczynienie - akt komunii duchowej", 0xF6DE8E, F_SREDNI);
        zawin_srodek(x + 34, KO_W - 68, ty + 38, "Jezu mój, wierzę, że jesteś obecny w Najświętszym Sakramencie. Kocham Cię nade wszystko "
                     "i pragnę Cię przyjąć do mojej duszy. Przyjdź do mego serca. Obejmuję Cię i jednoczę się z Tobą. "
                     "Nie dozwól, abym kiedykolwiek miał się od Ciebie odłączyć. Amen.", 0xE8ECF8, F_TEKST, 20);
        fb_napis_srodek(x, KO_W, ty + 170, "♫  Schmücke dich, o liebe Seele - J. S. Bach", 0x9AD7E6, F_TEKST);
        btn(x + KO_W / 2 - 70, ty + 202, 140, 38, "Zakończ", 0xFFFFFF, TEKST, F_TEKST_B);
    }
}

static void ko_dalej(void) {
    if (ko_etap == 0) { ko_etap = 1; ko_t0 = zegar_ms(); gui_muzyka_stop(); }
    else if (ko_etap == 3) { ko_etap = 4; ko_t0 = zegar_ms(); }
    else if (ko_etap == 5) { gui_apka_zamknij(AP_KOMUNIA); return; }
    else return;
    gui_apka_brudne(AP_KOMUNIA, 0, 410, KO_W, KO_H - 410);
}

static void ko_klik(int lx, int ly) {
    if (ko_etap == 0 && w_prost(lx, ly, KO_W / 2 - 190, 420 + 118, 380, 46)) ko_dalej();
    else if (ko_etap == 3 && w_prost(lx, ly, KO_W / 2 - 90, 420 + 108, 180, 46)) ko_dalej();
    else if (ko_etap == 5 && w_prost(lx, ly, KO_W / 2 - 70, 420 + 202, 140, 38)) ko_dalej();
}

static int ko_klawisz(int k) { if (k == KEY_ENTER || k == ' ') { ko_dalej(); return 1; } return 0; }

static void ko_tik(u32 t) {
    static u32 ost;
    if (ko_etap == 1 && t - ko_t0 > 2200) {
        ko_etap = 2; ko_seria = 0; ko_faza = 0; ko_t0 = ko_tp = t;
        if (audio_jest) ko_dzw_ms = dzw_dzwonki(3) + 200;     /* prawdziwe dzwonki ministranckie */
        gui_apka_brudne(AP_KOMUNIA, 0, 410, KO_W, KO_H - 410);
    }
    else if (ko_etap == 2 && audio_jest) {
        if (t - ko_t0 >= ko_dzw_ms) { ko_etap = 3; gui_apka_brudne(AP_KOMUNIA, 0, 410, KO_W, KO_H - 410); }
    }
    else if (ko_etap == 2) {                                  /* trzy serie dzwonków z głośniczka PC */
        if (ko_faza == 0) {
            if (t - ko_tp >= 28) { ko_tp = t; ko_przel = !ko_przel; glosnik_ton(ko_przel ? 2093 : 2637); }
            if (t - ko_t0 >= 450) { glosnik_cisza(); ko_faza = 1; ko_t0 = t; }
        } else if (t - ko_t0 >= 220) {
            if (++ko_seria == 3) { ko_etap = 3; gui_apka_brudne(AP_KOMUNIA, 0, 410, KO_W, KO_H - 410); }
            else { ko_faza = 0; ko_t0 = ko_tp = t; }
        }
    } else if (ko_etap == 4 && t - ko_t0 > 1400) { ko_etap = 5; gui_muzyka_graj(5); gui_apka_brudne(AP_KOMUNIA, 0, 410, KO_W, KO_H - 410); }
    if (t - ost > 110) {                                      /* migoczące gwiazdki */
        ost = t;
        for (int i = 0; i < 7; i++) gui_apka_brudne(AP_KOMUNIA, GW[i][0] - 8, GW[i][1] - 8, 17, 17);
    }
}

static void ko_otw(void) { ko_etap = 0; }
static void ko_zamk(void) {
    if (ko_etap == 2 && !audio_jest) glosnik_cisza();
    if (ko_etap == 5) gui_muzyka_stop();
    ko_etap = 0;
}

/* ====================================================================== */
/*  Ogłoszenia parafialne - tablica korkowa                                */
/* ====================================================================== */

#define OG_W 930
#define OG_H 580
#define OG_NW 205
#define OG_NH 128
static char og_wej[128];
static int og_miga;
static const u32 OG_KOLORY[4] = { 0xFFF3A6, 0xFFD6E5, 0xD2ECFF, 0xD8F5CB };

static void og_poz(int i, int *nx, int *ny) {
    *nx = 31 + (i % 4) * (OG_NW + 16);
    *ny = 88 + (i / 4) * (OG_NH + 12) + ((i * 37) % 7) - 3;
}

static void og_rysuj(int x, int y) {
    fb_prost(x, y, OG_W, OG_H, 0xC69A62);
    u32 s = 777;
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    for (int k = 0; k < 2600; k++) {                          /* faktura korka */
        s = s * 1103515245u + 12345u; int px = (int)((s >> 8) % OG_W);
        s = s * 1103515245u + 12345u; int py = (int)((s >> 8) % (OG_H - 56));
        if (x + px < kx - 2 || x + px > kx + kw || y + py < ky - 2 || y + py > ky + kh) continue;
        fb_prost(x + px, y + py, 2, 2, (s >> 20) & 1 ? 0xB08550 : 0xD6AD78);
    }
    fb_prost(x, y, OG_W, 10, 0x7B4A22); fb_prost(x, y + OG_H - 66, OG_W, 10, 0x7B4A22);
    fb_prost(x, y, 10, OG_H - 56, 0x7B4A22); fb_prost(x + OG_W - 10, y, 10, OG_H - 56, 0x7B4A22);
    /* nagłówek */
    int hx = x + OG_W / 2 - 200;
    fb_cien(hx, y + 22, 400, 54, 3, 10, 70, 4);
    fb_zaokr(hx, y + 22, 400, 54, 3, 0xFFFDF7, 256);
    fb_napis_srodek(hx, 400, y + 28, "OGŁOSZENIA PARAFIALNE", 0xB42A32, F_SREDNI);
    fb_napis_srodek(hx, 400, y + 52, parafia, SZARY_T, F_MALY);
    fb_kolo_aa(hx + 14, y + 32, 5, 0x2F62D9, 256); fb_kolo_aa(hx + 386, y + 32, 5, 0x2F62D9, 256);
    /* karteczki */
    for (int i = 0; i < ogl_n; i++) {
        int nx, ny; og_poz(i, &nx, &ny);
        nx += x; ny += y;
        fb_cien(nx, ny, OG_NW, OG_NH, 3, 10, 80, 4);
        fb_zaokr(nx, ny, OG_NW, OG_NH, 3, OG_KOLORY[i % 4], 256);
        fb_kolo_aa(nx + OG_NW / 2, ny + 10, 7, 0xD6333F, 256);
        fb_kolo_aa(nx + OG_NW / 2 - 2, ny + 8, 2, 0xFFB8BE, 256);
        char nr[8]; ksprintf(nr, sizeof nr, "%d.", i + 1);
        fb_napis(nx + 12, ny + 18, nr, 0x8A6A20, F_TEKST_B);
        fb_napis(nx + OG_NW - 22, ny + 6, "×", 0x8A8070, F_SREDNI);
        int kx2, ky2, kw2, kh2;
        fb_klip_pobierz(&kx2, &ky2, &kw2, &kh2);
        fb_klip_zawez(nx, ny, OG_NW, OG_NH - 6);
        fb_napis_zawin(nx + 12, ny + 40, OG_NW - 24, ogloszenia[i], 0x2E2A22, F_MALY, 16);
        fb_klip(kx2, ky2, kw2, kh2);
    }
    if (!ogl_n) {
        int nx = x + OG_W / 2 - 150, ny = y + 200;
        fb_cien(nx, ny, 300, 120, 3, 10, 80, 4);
        fb_zaokr(nx, ny, 300, 120, 3, 0xFFF3A6, 256);
        fb_kolo_aa(nx + 150, ny + 10, 7, 0xD6333F, 256);
        zawin_srodek(nx + 20, 260, ny + 40, "Brak ogłoszeń. Proboszcz ma dzień wolny.", 0x2E2A22, F_TEKST_B, 22);
    }
    /* dół: nowe ogłoszenie */
    fb_prost(x, y + OG_H - 56, OG_W, 56, 0xF3F4F7);
    pole_rysuj(x + 16, y + OG_H - 48, OG_W - 184, 40, og_wej, "Napisz nowe ogłoszenie i naciśnij Enter…", 1);
    btn(x + OG_W - 156, y + OG_H - 48, 140, 40, "Przypnij", gui_akcent(), 0xFFFFFF, F_TEKST_B);
}

static void og_przypnij(void) {
    if (!og_wej[0]) { gui_komunikat("Najpierw napisz treść ogłoszenia w polu na dole tablicy."); return; }
    if (ogl_n >= OGL_N) { gui_komunikat("Tablica pełna! Zdejmij któreś ogłoszenie, zanim kościelny przybije nowe gwoździem."); return; }
    k_strncpy(ogloszenia[ogl_n++], og_wej, sizeof ogloszenia[0]);
    og_wej[0] = 0;
    dzw_efekt(E_KLIK);
    gui_apka_cala(AP_OGLOSZENIA);
}

static void og_klik(int lx, int ly) {
    if (w_prost(lx, ly, OG_W - 156, OG_H - 48, 140, 40)) { og_przypnij(); return; }
    for (int i = 0; i < ogl_n; i++) {
        int nx, ny; og_poz(i, &nx, &ny);
        if (w_prost(lx, ly, nx + OG_NW - 28, ny, 28, 28)) {
            for (int j = i; j < ogl_n - 1; j++) k_strncpy(ogloszenia[j], ogloszenia[j + 1], sizeof ogloszenia[0]);
            ogl_n--;
            gui_apka_cala(AP_OGLOSZENIA);
            return;
        }
    }
}

static int og_klawisz(int k) {
    if (k == KEY_ESC) return 0;
    if (k == KEY_ENTER) { og_przypnij(); return 1; }
    if (pole_klawisz(og_wej, sizeof og_wej, k)) gui_apka_brudne(AP_OGLOSZENIA, 0, OG_H - 56, OG_W, 56);
    return 1;
}
static void og_tik(u32 t) { (void)t; if (miga() != og_miga) { og_miga = miga(); gui_apka_brudne(AP_OGLOSZENIA, 16, OG_H - 48, OG_W - 184, 40); } }

/* ====================================================================== */
/*  Porządek Mszy z odliczaniem                                            */
/* ====================================================================== */

#define MS_W 560
#define MS_H 520
static int ms_edyt = -1, ms_miga, ms_ost_min = -1;
static char ms_wej[64];

static int ms_wiersz_dnia(int r, int m, int d) {
    dzien_lit_t l; liturgia(r, m, d, &l);
    int dow = dzien_tygodnia(dni_od_epoki(r, m, d));
    if (dow == 0 || l.ranga <= 1) return 0;
    return dow == 6 ? 2 : 1;
}

static int ms_godziny(const char *s, int *t, int max) {
    int n = 0;
    while (*s && n < max) {
        if (*s >= '0' && *s <= '9') {
            int h = 0, c = 0;
            while (*s >= '0' && *s <= '9' && c < 3) { h = h * 10 + (*s - '0'); s++; c++; }
            if ((*s == ':' || *s == '.') && s[1] >= '0' && s[1] <= '9' && s[2] >= '0' && s[2] <= '9' && h < 24) {
                t[n++] = h * 60 + (s[1] - '0') * 10 + (s[2] - '0');
                s += 3;
            }
        } else s++;
    }
    return n;
}

static void ms_rysuj(int x, int y) {
    u32 ak = gui_akcent();
    fb_prost(x, y, MS_W, MS_H, 0xFAFBFC);
    fb_ikona_rys(x + 24, y + 22, 10, 48);
    fb_napis(x + 88, y + 20, "Porządek Mszy Świętych", TEKST, F_H2);
    fb_napis(x + 88, y + 52, parafia, SZARY_T, F_TEKST);

    /* odliczanie */
    czas_t c; rtc_czytaj(&c);
    int teraz = c.godz * 60 + c.min, dzis = dni_od_epoki(c.rok, c.mies, c.dzien);
    int t[16], n = ms_godziny(msze[ms_wiersz_dnia(c.rok, c.mies, c.dzien)].godz, t, 16);
    int kiedy = -1, godz = 0, za = 0, trwa = 0;
    for (int i = 0; i < n; i++) {
        if (t[i] <= teraz && teraz - t[i] < 45) { trwa = 1; kiedy = 0; godz = t[i]; break; }
        if (t[i] > teraz) { kiedy = 0; godz = t[i]; za = t[i] - teraz; break; }
    }
    if (kiedy < 0) {
        int r, m, d; data_z_dni(dzis + 1, &r, &m, &d);
        int n2 = ms_godziny(msze[ms_wiersz_dnia(r, m, d)].godz, t, 16);
        if (n2) { kiedy = 1; godz = t[0]; za = 24 * 60 - teraz + t[0]; }
    }
    fb_zaokr_gradient(x + 16, y + 88, MS_W - 32, 118, 14, ak, fb_mieszaj(ak, 0x000000, 100));
    fb_napis(x + 34, y + 100, "Najbliższa Msza", 0xDDE6F5, F_MALY);
    char s[120];
    const char *uwaga;
    if (kiedy < 0) { k_strncpy(s, "brak w planie", sizeof s); uwaga = "Wpisz godziny Mszy poniżej - wtedy policzę, ile zostało czasu."; }
    else {
        ksprintf(s, sizeof s, "%s o %d:%02d", kiedy ? "jutro" : "dziś", godz / 60, godz % 60);
        if (trwa) uwaga = "Msza właśnie trwa! Wchodź po cichu, najlepiej bocznymi drzwiami.";
        else if (kiedy) uwaga = "Na dziś to już wszystko. Jutro rano - nie zaśpij!";
        else if (za <= 10) uwaga = "Biegiem! Ministranci już zapalają świece.";
        else if (za <= 30) uwaga = "Czas wkładać buty. Miejsca w ostatniej ławce już zajęte.";
        else if (za <= 120) uwaga = "Zdążysz spokojnie - może nawet usiądziesz z przodu.";
        else uwaga = "Mnóstwo czasu. Zdążysz jeszcze ugotować rosół.";
    }
    fb_napis(x + 34, y + 116, s, 0xFFFFFF, F_DUZY);
    if (kiedy >= 0 && !trwa) {
        if (za >= 60) ksprintf(s, sizeof s, "za %d h %d min", za / 60, za % 60);
        else ksprintf(s, sizeof s, "za %d min", za);
        fb_napis(x + MS_W - 34 - fb_napis_szer(s, F_SREDNI), y + 122, s, 0xFFFFFF, F_SREDNI);
    }
    fb_napis_zawin(x + 34, y + 160, MS_W - 68, uwaga, 0xF0F4FB, F_TEKST, 19);

    /* lista */
    int dzisw = ms_wiersz_dnia(c.rok, c.mies, c.dzien);
    for (int i = 0; i < MSZE_N; i++) {
        int ry = y + 222 + i * 66;
        if (i) fb_prost(x + 24, ry - 2, MS_W - 48, 1, 0xE5E7EB);
        if (i == dzisw) fb_zaokr(x + 16, ry + 8, 4, 46, 2, ak, 256);
        fb_napis(x + 32, ry + 8, msze[i].etyk, TEKST, F_TEKST_B);
        if (i == ms_edyt) {
            pole_rysuj(x + 32, ry + 28, MS_W - 150, 34, ms_wej, "np. 7:00, 18:00", 1);
            btn(x + MS_W - 108, ry + 28, 84, 34, "Zapisz", ak, 0xFFFFFF, F_TEKST_B);
        } else {
            fb_napis_zawin(x + 32, ry + 28, MS_W - 150, msze[i].godz, 0x4B5160, F_TEKST, 18);
            btn(x + MS_W - 96, ry + 14, 72, 30, "Zmień", 0xFFFFFF, TEKST, F_TEKST);
        }
    }
    fb_napis(x + 24, y + MS_H - 28, "Godziny wpisuj dowolnie, np. 7:00, 9:00 (suma), 18:00.", 0x9AA0AA, F_MALY);
}

static void ms_klik(int lx, int ly) {
    for (int i = 0; i < MSZE_N; i++) {
        int ry = 222 + i * 66;
        if (i == ms_edyt && w_prost(lx, ly, MS_W - 108, ry + 28, 84, 34)) {
            k_strncpy(msze[i].godz, ms_wej, sizeof msze[i].godz); ms_edyt = -1; gui_apka_cala(AP_MSZE); return;
        }
        if (i != ms_edyt && w_prost(lx, ly, MS_W - 96, ry + 14, 72, 30)) {
            ms_edyt = i; k_strncpy(ms_wej, msze[i].godz, sizeof ms_wej); gui_apka_cala(AP_MSZE); return;
        }
    }
}

static int ms_klawisz(int k) {
    if (ms_edyt < 0) return 0;
    if (k == KEY_ESC) ms_edyt = -1;
    else if (k == KEY_ENTER) { k_strncpy(msze[ms_edyt].godz, ms_wej, sizeof msze[0].godz); ms_edyt = -1; }
    else if (!pole_klawisz(ms_wej, sizeof ms_wej, k)) return 1;
    gui_apka_cala(AP_MSZE);
    return 1;
}

static void ms_tik(u32 t) {
    static u32 ost;
    if (t - ost >= 1000) {
        ost = t;
        czas_t c; rtc_czytaj(&c);
        if (c.min != ms_ost_min) { ms_ost_min = c.min; gui_apka_brudne(AP_MSZE, 0, 80, MS_W, 140); }
    }
    if (ms_edyt >= 0 && miga() != ms_miga) { ms_miga = miga(); gui_apka_brudne(AP_MSZE, 0, 222 + ms_edyt * 66 + 26, MS_W, 40); }
}
static void ms_zamk(void) { ms_edyt = -1; }

/* ====================================================================== */
/*  Menedżer zadań                                                         */
/* ====================================================================== */

#define ZA_W 620
#define ZA_H 560
#define ZA_RZ 36
static int za_wybr, za_n, za_id[20];
static u32 za_sek;

static int za_lista(void) {
    za_id[0] = -1000;                               /* Anioł Stróż - zawsze na górze */
    za_n = 1 + gui_okna_lista(za_id + 1, 18);
    if (za_wybr >= za_n) za_wybr = za_n - 1;
    return za_n;
}

static void za_rysuj(int x, int y) {
    za_lista();
    fb_prost(x, y, ZA_W, ZA_H, 0xFAFBFC);
    fb_napis(x + 24, y + 18, "Menedżer zadań", TEKST, F_H2);
    fb_napis(x + 24, y + 50, "Wszystkie programy działają jednocześnie - jak chór, każdy swoim głosem.", SZARY_T, F_TEKST);
    int hy = y + 84;
    fb_napis(x + 60, hy, "Program", SZARY_T, F_MALY);
    fb_napis(x + 300, hy, "Stan", SZARY_T, F_MALY);
    fb_napis(x + 410, hy, "Łaska", SZARY_T, F_MALY);
    fb_napis(x + 528, hy, "Pamięć", SZARY_T, F_MALY);
    fb_prost(x + 16, hy + 20, ZA_W - 32, 1, 0xE5E7EB);
    static const char *STANY[3] = { "Aktywny", "W tle", "Ukryty" };
    int max = (ZA_H - 110 - 74) / ZA_RZ;
    for (int i = 0; i < za_n && i < max; i++) {
        int ry = hy + 26 + i * ZA_RZ, id = za_id[i];
        if (i == za_wybr) fb_zaokr(x + 12, ry, ZA_W - 24, ZA_RZ - 4, 8, fb_mieszaj(0xFFFFFF, gui_akcent(), 40), 256);
        char t[80], m[24];
        int laska;
        if (id == -1000) {
            fb_ikona_rys(x + 24, ry + 4, 11, 24);
            fb_napis(x + 60, ry + 8, "Anioł Stróż", TEKST, F_TEKST_B);
            fb_napis(x + 300, ry + 8, "Czuwa", 0x16A34A, F_TEKST);
            laska = 100; k_strncpy(m, "∞", sizeof m);
        } else {
            fb_ikona_rys(x + 24, ry + 4, gui_okno_ikona(id), 24);
            fb_napis_przytnij(t, sizeof t, gui_okno_tytul(id), 230, F_TEKST);
            fb_napis(x + 60, ry + 8, t, TEKST, F_TEKST);
            int st = gui_okno_stan(id);
            fb_napis(x + 300, ry + 8, STANY[st], st == 0 ? gui_akcent() : SZARY_T, F_TEKST);
            laska = 35 + (int)(((u32)(id + 3) * 2654435761u + za_sek * 40503u) >> 24) % 60;
            ksprintf(m, sizeof m, "%d KB", gui_okno_pamiec_kb(id));
        }
        fb_zaokr(x + 410, ry + 12, 96, 10, 5, 0xE5E7EB, 256);
        fb_zaokr(x + 410, ry + 12, 96 * laska / 100 < 10 ? 10 : 96 * laska / 100, 10, 5, 0xE0B23C, 256);
        fb_napis(x + 528, ry + 8, m, 0x4B5160, F_TEKST);
    }
    if (za_n > max) {
        char w[48]; ksprintf(w, sizeof w, "… i jeszcze %d", za_n - max);
        fb_napis(x + 60, hy + 26 + max * ZA_RZ + 4, w, SZARY_T, F_MALY);
    }
    int by = y + ZA_H - 56;
    fb_prost(x, by - 10, ZA_W, 1, 0xE5E7EB);
    btn(x + 24, by, 130, 38, "Przełącz", 0xFFFFFF, TEKST, F_TEKST_B);
    btn(x + 164, by, 170, 38, "Zakończ zadanie", 0xD6333F, 0xFFFFFF, F_TEKST_B);
    char s[64]; ksprintf(s, sizeof s, "Procesy: %d  ·  Łaska: bez limitu", za_n);
    fb_napis(x + ZA_W - 24 - fb_napis_szer(s, F_MALY), by + 12, s, SZARY_T, F_MALY);
}

static void za_akcja(int zakoncz) {
    za_lista();
    int id = za_id[za_wybr];
    if (id == -1000) {
        gui_komunikat(zakoncz ? "Nie można zakończyć procesu „Anioł Stróż”. Proces jest chroniony z góry."
                              : "Anioł Stróż nie ma okna. Działa dyskretnie i zawsze w tle.");
        return;
    }
    if (zakoncz) gui_okno_zamknij_id(id); else gui_okno_przelacz(id);
    gui_apka_cala(AP_ZADANIA);
}

static void za_klik(int lx, int ly) {
    int hy = 84;
    if (ly >= hy + 26 && ly < ZA_H - 74) {
        int i = (ly - hy - 26) / ZA_RZ;
        za_lista();
        if (i < za_n) { za_wybr = i; gui_apka_cala(AP_ZADANIA); }
    } else if (w_prost(lx, ly, 24, ZA_H - 56, 130, 38)) za_akcja(0);
    else if (w_prost(lx, ly, 164, ZA_H - 56, 170, 38)) za_akcja(1);
}

static int za_klawisz(int k) {
    za_lista();
    if (k == KEY_UP && za_wybr > 0) za_wybr--;
    else if (k == KEY_DOWN && za_wybr < za_n - 1) za_wybr++;
    else if (k == KEY_ENTER) { za_akcja(0); return 1; }
    else return 0;
    gui_apka_cala(AP_ZADANIA);
    return 1;
}

static void za_tik(u32 t) { if (t / 1000 != za_sek) { za_sek = t / 1000; gui_apka_brudne(AP_ZADANIA, 0, 80, ZA_W, ZA_H - 80); } }

/* ====================================================================== */

static void nic(void) {}

const apka_t apki[AP_N] = {
    { "Tablica pieśni",      3, PI_W, PI_H, pi_rysuj, pi_klik, pi_klawisz, pi_tik, nic,    pi_zamk },
    { "Modlitewnik",         6, MO_W, MO_H, mo_rysuj, mo_klik, mo_klawisz, mo_tik, nic,    nic },
    { "Spowiedź",            7, SP_W, SP_H, sp_rysuj, sp_klik, sp_klawisz, sp_tik, nic,    sp_zamk },
    { "Komunia święta",      8, KO_W, KO_H, ko_rysuj, ko_klik, ko_klawisz, ko_tik, ko_otw, ko_zamk },
    { "Ogłoszenia parafialne", 9, OG_W, OG_H, og_rysuj, og_klik, og_klawisz, og_tik, nic,  nic },
    { "Porządek Mszy",      10, MS_W, MS_H, ms_rysuj, ms_klik, ms_klawisz, ms_tik, nic,    ms_zamk },
    { "Menedżer zadań",     13, ZA_W, ZA_H, za_rysuj, za_klik, za_klawisz, za_tik, nic,    nic },
};
