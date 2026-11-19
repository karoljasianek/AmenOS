/* AmenOS - powłoka (wiersz poleceń) i ekrany parafialne */
#include "kernel.h"

/* ====================================================================== */
/*  Dane parafii (w pamięci RAM - znikają po wyłączeniu komputera)         */
/* ====================================================================== */

static char parafia[64] = "Parafia Rzymskokatolicka";

#define PIESNI_N 6
static const char *piesni_etyk[PIESNI_N] = {
    "Na wejście", "Przygotowanie darów", "Na Komunię", "Dziękczynienie", "Na zakończenie", "Dodatkowa"
};
static char piesni[PIESNI_N][6];

#define MSZE_N 4
static struct { char etyk[32]; char godz[64]; } msze[MSZE_N] = {
    { "Niedziele i święta", "7:00, 9:00, 10:30 (suma), 12:00, 18:00" },
    { "Dni powszednie",     "7:00, 18:00" },
    { "Sobota",             "7:00, 18:00 (z liturgii niedzielnej)" },
    { "Spowiedź",           "pół godziny przed każdą Mszą" },
};

#define OGL_N 12
static char ogloszenia[OGL_N][128] = {
    "Witamy w systemie AmenOS! Ogłoszenia dodasz poleceniem: ogloszenia dodaj <treść>",
};
static int ogl_n = 1;

static u32 pamiec_kb;

/* ====================================================================== */
/*  Pomocnicze funkcje ekranowe                                            */
/* ====================================================================== */

static void wysrodkuj(int y, const char *s, u8 a) {
    int x = (VGA_W - (int)k_utf8_len(s)) / 2;
    if (x < 0) x = 0;
    vga_puts_at(x, y, s, a);
}

static void ramka(int x, int y, int w, int h, u8 a) {
    vga_glyph_at(x, y, 0xC9, a); vga_glyph_at(x + w - 1, y, 0xBB, a);
    vga_glyph_at(x, y + h - 1, 0xC8, a); vga_glyph_at(x + w - 1, y + h - 1, 0xBC, a);
    for (int i = 1; i < w - 1; i++) { vga_glyph_at(x + i, y, 0xCD, a); vga_glyph_at(x + i, y + h - 1, 0xCD, a); }
    for (int j = 1; j < h - 1; j++) { vga_glyph_at(x, y + j, 0xBA, a); vga_glyph_at(x + w - 1, y + j, 0xBA, a); }
}

/* plansza pełnoekranowa z ramką i tytułem */
static void plansza(const char *tytul, u8 tlo_attr, u8 ramka_attr) {
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', tlo_attr);
    ramka(0, 0, VGA_W, VGA_H, ramka_attr);
    char t[96];
    ksprintf(t, sizeof t, " %s ", tytul);
    wysrodkuj(0, t, ramka_attr);
}

static void czekaj_i_wroc(void) {
    kbd_getkey();
    vga_color(SZARY, CZARNY);
    vga_clear();
    vga_cursor(1);
}

/* zawijanie tekstu po słowach; zwraca liczbę zajętych wierszy */
static int zawin(const char *s, int x, int y, int w, u8 a) {
    u32 cp[256]; int n = 0;
    while (*s && n < 256) cp[n++] = k_utf8_next(&s);
    int linie = 0, i = 0;
    while (i < n) {
        while (i < n && cp[i] == ' ') i++;
        if (i >= n) break;
        int koniec = i + w;
        if (koniec >= n) koniec = n;
        else {
            int k = koniec;
            while (k > i && cp[k] != ' ') k--;
            if (k > i) koniec = k;
        }
        for (int j = i; j < koniec; j++) vga_glyph_at(x + j - i, y + linie, vga_map(cp[j]), a);
        linie++;
        i = koniec;
    }
    return linie ? linie : 1;
}

/* ---------- duże cyfry 5x7 ---------- */
static const u8 cyfry[11][7] = {
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },
    { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 },
};

/* rysuje znak (0-9, 10 = dwukropek); pelne=1 -> 7 wierszy, 0 -> 4 wiersze (półbloki) */
static void duzy_znak(int x, int y, int z, int pelne, u8 a) {
    int c0 = (z == 10) ? 1 : 0, c1 = (z == 10) ? 3 : 5;
    if (pelne) {
        for (int r = 0; r < 7; r++)
            for (int c = c0; c < c1; c++) {
                u8 g = (cyfry[z][r] & (0x10 >> c)) ? 0xDB : ' ';
                vga_glyph_at(x + 2 * (c - c0), y + r, g, a);
                vga_glyph_at(x + 2 * (c - c0) + 1, y + r, g, a);
            }
    } else {
        for (int r = 0; r < 4; r++) {
            u8 gora = cyfry[z][2 * r], dol = (2 * r + 1 < 7) ? cyfry[z][2 * r + 1] : 0;
            for (int c = c0; c < c1; c++) {
                int t = gora & (0x10 >> c), b = dol & (0x10 >> c);
                u8 g = (t && b) ? 0xDB : t ? 0xDF : b ? 0xDC : ' ';
                vga_glyph_at(x + 2 * (c - c0), y + r, g, a);
                vga_glyph_at(x + 2 * (c - c0) + 1, y + r, g, a);
            }
        }
    }
}

static int szer_znaku(int z) { return z == 10 ? 4 : 10; }

/* ---------- duże litery 5x7 (+ miejsce na kreski i ogonki) ---------- */
static const u8 alfabet[26][7] = {
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 },
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },
    { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
};

/* kształt litery w 12 wierszach: 0-1 kreska/kropka, 3-9 litera, 10-11 ogonek */
static void ksztalt_litery(u32 c, u8 w[12]) {
    memset(w, 0, 12);
    if (c >= 'a' && c <= 'z') c -= 32;
    int kreska = 0, kropka = 0, ogonek = 0;
    switch (c) {
    case 0x104: case 0x105: c = 'A'; ogonek = 1; break;
    case 0x118: case 0x119: c = 'E'; ogonek = 1; break;
    case 0x106: case 0x107: c = 'C'; kreska = 1; break;
    case 0x143: case 0x144: c = 'N'; kreska = 1; break;
    case 0x0D3: case 0x0F3: c = 'O'; kreska = 1; break;
    case 0x15A: case 0x15B: c = 'S'; kreska = 1; break;
    case 0x179: case 0x17A: c = 'Z'; kreska = 1; break;
    case 0x17B: case 0x17C: c = 'Z'; kropka = 1; break;
    case 0x141: case 0x142: {
        static const u8 l[7] = { 0x08, 0x08, 0x0A, 0x0C, 0x18, 0x08, 0x0F };
        memcpy(w + 3, l, 7); return;
    }
    }
    if (c >= 'A' && c <= 'Z') memcpy(w + 3, alfabet[c - 'A'], 7);
    else if (c >= '0' && c <= '9') memcpy(w + 3, cyfry[c - '0'], 7);
    else if (c == ':') memcpy(w + 3, cyfry[10], 7);
    else if (c == '.') w[9] = 0x04;
    else if (c == '-') w[6] = 0x0E;
    else if (c == '!') { for (int i = 3; i < 8; i++) w[i] = 0x04; w[9] = 0x04; }
    if (kreska) { w[0] = 0x02; w[1] = 0x04; }
    if (kropka) w[1] = 0x04;
    if (ogonek) { w[10] = 0x02; w[11] = 0x03; }
}

/* napis dużymi literami (6 wierszy wysokości, 6 kolumn na znak), wyśrodkowany */
static void duzy_napis(int y, const char *s, u8 a) {
    int n = (int)k_utf8_len(s);
    int x = (VGA_W - (n * 6 - 1)) / 2;
    if (x < 0) x = 0;
    while (*s) {
        u8 w[12];
        ksztalt_litery(k_utf8_next(&s), w);
        for (int r = 0; r < 6; r++)
            for (int c = 0; c < 5; c++) {
                int t = w[2 * r] & (0x10 >> c), b = w[2 * r + 1] & (0x10 >> c);
                vga_glyph_at(x + c, y + r, (t && b) ? 0xDB : t ? 0xDF : b ? 0xDC : ' ', a);
            }
        x += 6;
    }
}

/* ====================================================================== */
/*  Liturgia na ekranie                                                    */
/* ====================================================================== */

static void wypisz_dzien(int r, int m, int d) {
    dzien_lit_t l;
    liturgia(r, m, d, &l);
    int dow = dzien_tygodnia(dni_od_epoki(r, m, d));
    vga_color(ZOLTY, CZARNY);
    kprintf("\n  %s, %d %s %d\n", dni_tyg_wielk[dow], d, mies_dop[m], r);
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("  ────────────────────────────────────────────────────────────\n");
    vga_color(SZARY, CZARNY);   kprintf("  Obchód:  ");
    vga_color(BIALY, CZARNY);   kprintf("%s\n", l.nazwa);
    if (l.uwaga[0]) { vga_color(SZARY, CZARNY); kprintf("           (%s)\n", l.uwaga); }
    vga_color(SZARY, CZARNY);   kprintf("  Okres:   %s\n", l.okres);
    kprintf("  Kolor:   ");
    vga_color(kolor_tekst(l.kolor), kolor_vga(l.kolor)); kprintf(" %s ", kolor_nazwa(l.kolor));
    vga_color(SZARY, CZARNY);
    kprintf("\n  Rok liturgiczny: czytania niedzielne - rok %c, powszednie - rok %s\n\n",
            l.cykl_niedz, l.cykl_powsz == 1 ? "I" : "II");
}

static void cmd_dzis(void) { czas_t c; rtc_czytaj(&c); wypisz_dzien(c.rok, c.mies, c.dzien); }

static int parsuj_date(char *s, int *r, int *m, int *d) {
    czas_t c; rtc_czytaj(&c);
    int cz[3] = { 0, 0, c.rok }, n = 0;
    char *p = s;
    while (n < 3) {
        char *start = p;
        while (*p && *p != '.' && *p != '-' && *p != '/') p++;
        char sep = *p; *p = 0;
        int ok; cz[n] = k_atoi(start, &ok);
        if (!ok) return 0;
        n++;
        if (!sep) break;
        p++;
    }
    if (n < 2) return 0;
    *d = cz[0]; *m = cz[1]; *r = cz[2];
    if (*r < 100) *r += 2000;
    return *m >= 1 && *m <= 12 && *d >= 1 && *d <= dni_w_miesiacu(*r, *m) && *r >= 1583 && *r <= 4099;
}

static void cmd_dzien(char *arg) {
    int r, m, d;
    if (!arg || !parsuj_date(arg, &r, &m, &d)) { kprintf("Użycie: dzien DD.MM[.RRRR]   np. dzien 24.12.2026\n"); return; }
    wypisz_dzien(r, m, d);
}

static void cmd_miesiac(char *a1, char *a2) {
    czas_t c; rtc_czytaj(&c);
    int m = c.mies, r = c.rok, ok = 1;
    if (a1) { m = k_atoi(a1, &ok); if (!ok || m < 1 || m > 12) { kprintf("Użycie: miesiac [MM [RRRR]]\n"); return; } }
    if (a2) { r = k_atoi(a2, &ok); if (!ok || r < 1583 || r > 4099) { kprintf("Rok poza zakresem (1583-4099)\n"); return; } }

    vga_color(SZARY, CZARNY);
    vga_clear();
    char t[48]; ksprintf(t, sizeof t, "%s %d", mies_mian_wielk[m], r);
    wysrodkuj(0, t, ATTR(ZOLTY, CZARNY));
    const char *nagl[7] = { "Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd" };
    int x0 = 19;
    for (int i = 0; i < 7; i++) vga_puts_at(x0 + i * 6 + 1, 2, nagl[i], ATTR(i == 6 ? JASNOCZERWONY : SZARY, CZARNY));

    int pierwszy = dni_od_epoki(r, m, 1);
    int kol = (dzien_tygodnia(pierwszy) + 6) % 7;     /* poniedziałek = 0 */
    int wiersz = 3;
    for (int d = 1; d <= dni_w_miesiacu(r, m); d++) {
        dzien_lit_t l; liturgia(r, m, d, &l);
        int dzis = (d == c.dzien && m == c.mies && r == c.rok);
        char kom[8]; ksprintf(kom, sizeof kom, dzis ? "[%2d]" : " %2d ", d);
        vga_puts_at(x0 + kol * 6, wiersz, kom, ATTR(dzis ? ZOLTY : kolor_tekst(l.kolor), kolor_vga(l.kolor)));
        if (++kol == 7) { kol = 0; wiersz++; }
    }
    if (kol) wiersz++;
    wiersz++;
    /* legenda */
    int lx = 6;
    for (int k = 0; k < 5; k++) {
        char n[16]; ksprintf(n, sizeof n, " %s ", kolor_nazwa(k));
        vga_puts_at(lx, wiersz, n, ATTR(kolor_tekst(k), kolor_vga(k)));
        lx += (int)k_utf8_len(n) + 2;
    }
    vga_puts_at(lx + 2, wiersz, "[ ] = dziś", ATTR(ZOLTY, CZARNY));
    wiersz += 2;

    /* najważniejsze obchody miesiąca */
    int wypisane = 0, max = VGA_H - 2 - wiersz;
    for (int d = 1; d <= dni_w_miesiacu(r, m); d++) {
        dzien_lit_t l; liturgia(r, m, d, &l);
        if (l.ranga > 4) continue;
        if (wypisane == max) { vga_puts_at(2, wiersz, "... (więcej: dzien DD.MM)", ATTR(CIEMNOSZARY, CZARNY)); wiersz++; break; }
        int dow = dzien_tygodnia(dni_od_epoki(r, m, d));
        char lin[120];
        ksprintf(lin, sizeof lin, "%2d %s", d, nagl[(dow + 6) % 7]);
        vga_puts_at(2, wiersz, lin, ATTR(dow == 0 ? JASNOCZERWONY : SZARY, CZARNY));
        vga_glyph_at(9, wiersz, 0xDB, ATTR(kolor_vga(l.kolor), CZARNY));
        vga_puts_at(11, wiersz, l.nazwa, ATTR(l.ranga <= 2 ? BIALY : SZARY, CZARNY));
        wiersz++; wypisane++;
    }
    vga_goto(0, wiersz + 1 < VGA_H ? wiersz + 1 : VGA_H - 1);
}

static void wiersz_daty(const char *opis, int dni) {
    int r, m, d; data_z_dni(dni, &r, &m, &d);
    vga_color(SZARY, CZARNY); kprintf("  %-38s", opis);
    vga_color(BIALY, CZARNY); kprintf("%2d %-13s", d, mies_dop[m]);
    vga_color(CIEMNOSZARY, CZARNY); kprintf("%s\n", dni_tyg[dzien_tygodnia(dni)]);
}

static void cmd_kalendarz(char *arg) {
    czas_t c; rtc_czytaj(&c);
    int r = c.rok, ok = 1;
    if (arg) { r = k_atoi(arg, &ok); if (!ok || r < 1583 || r > 4099) { kprintf("Użycie: kalendarz [RRRR] (1583-4099)\n"); return; } }
    int E = wielkanoc(r), adw = adwent1(r);
    int jan6 = dni_od_epoki(r, 1, 6);
    vga_color(ZOLTY, CZARNY);
    kprintf("\n  Kalendarz świąt ruchomych na rok %d\n", r);
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("  ─────────────────────────────────────────────────────────────\n");
    wiersz_daty("Chrzest Pański", jan6 + (7 - dzien_tygodnia(jan6)));
    wiersz_daty("Środa Popielcowa", E - 46);
    wiersz_daty("Niedziela Palmowa", E - 7);
    wiersz_daty("Wielki Czwartek", E - 3);
    wiersz_daty("Wielki Piątek", E - 2);
    wiersz_daty("Wielkanoc - Zmartwychwstanie Pańskie", E);
    wiersz_daty("Niedziela Miłosierdzia Bożego", E + 7);
    wiersz_daty("Wniebowstąpienie Pańskie", E + 42);
    wiersz_daty("Zesłanie Ducha Świętego", E + 49);
    wiersz_daty("Najświętszej Trójcy", E + 56);
    wiersz_daty("Boże Ciało", E + 60);
    wiersz_daty("Najświętszego Serca Pana Jezusa", E + 68);
    wiersz_daty("Chrystusa Króla Wszechświata", adw - 7);
    wiersz_daty("I Niedziela Adwentu", adw);
    dzien_lit_t l; liturgia(r, 6, 1, &l);
    vga_color(SZARY, CZARNY);
    kprintf("\n  Rok liturgiczny %d: czytania niedzielne - rok %c, powszednie - rok %s\n\n",
            r, l.cykl_niedz, l.cykl_powsz == 1 ? "I" : "II");
}

/* ====================================================================== */
/*  Modlitwy                                                               */
/* ====================================================================== */

typedef struct { const char *tytul; const char *linie[20]; } modlitwa_t;

static const modlitwa_t modlitwy[] = {
    { "Ojcze nasz", {
        "Ojcze nasz, któryś jest w niebie,", "święć się imię Twoje,", "przyjdź królestwo Twoje,",
        "bądź wola Twoja jako w niebie, tak i na ziemi.", "Chleba naszego powszedniego daj nam dzisiaj",
        "i odpuść nam nasze winy,", "jako i my odpuszczamy naszym winowajcom,",
        "i nie wódź nas na pokuszenie,", "ale nas zbaw ode złego.", "Amen.", 0 } },
    { "Zdrowaś Maryjo", {
        "Zdrowaś Maryjo, łaski pełna, Pan z Tobą,", "błogosławionaś Ty między niewiastami",
        "i błogosławiony owoc żywota Twojego, Jezus.", "", "Święta Maryjo, Matko Boża,",
        "módl się za nami grzesznymi", "teraz i w godzinę śmierci naszej.", "Amen.", 0 } },
    { "Chwała Ojcu", {
        "Chwała Ojcu i Synowi, i Duchowi Świętemu.", "Jak była na początku, teraz i zawsze,",
        "i na wieki wieków.", "Amen.", 0 } },
    { "Skład Apostolski (Wierzę w Boga)", {
        "Wierzę w Boga, Ojca wszechmogącego,", "Stworzyciela nieba i ziemi.",
        "I w Jezusa Chrystusa, Syna Jego jedynego, Pana naszego,", "który się począł z Ducha Świętego,",
        "narodził się z Maryi Panny,", "umęczon pod Ponckim Piłatem,", "ukrzyżowan, umarł i pogrzebion.",
        "Zstąpił do piekieł. Trzeciego dnia zmartwychwstał.", "Wstąpił na niebiosa,",
        "siedzi po prawicy Boga Ojca wszechmogącego.", "Stamtąd przyjdzie sądzić żywych i umarłych.",
        "Wierzę w Ducha Świętego, święty Kościół powszechny,", "świętych obcowanie, grzechów odpuszczenie,",
        "ciała zmartwychwstanie, żywot wieczny.", "Amen.", 0 } },
    { "Pod Twoją obronę", {
        "Pod Twoją obronę uciekamy się, święta Boża Rodzicielko,",
        "naszymi prośbami racz nie gardzić w potrzebach naszych,",
        "ale od wszelakich złych przygód racz nas zawsze wybawiać,", "Panno chwalebna i błogosławiona.",
        "", "O Pani nasza, Orędowniczko nasza, Pośredniczko nasza, Pocieszycielko nasza.",
        "Z Synem swoim nas pojednaj, Synowi swojemu nas polecaj,", "swojemu Synowi nas oddawaj.", "Amen.", 0 } },
    { "Aniele Boży", {
        "Aniele Boży, stróżu mój,", "ty zawsze przy mnie stój.", "Rano, wieczór, we dnie, w nocy",
        "bądź mi zawsze ku pomocy.", "Strzeż duszy, ciała mego", "i doprowadź mnie do żywota wiecznego.",
        "Amen.", 0 } },
    { "Akt żalu", {
        "Ach, żałuję za me złości", "jedynie dla Ciebie, Boże,", "bom obraził Twą dobroć niezmierzoną.",
        "Ach, żałuję i poprawę obiecuję,", "dopomóż mi, Panie Boże.", "Amen.", 0 } },
    { "Spowiadam się Bogu", {
        "Spowiadam się Bogu wszechmogącemu", "i wam, bracia i siostry,",
        "że bardzo zgrzeszyłem myślą, mową, uczynkiem i zaniedbaniem:",
        "moja wina, moja wina, moja bardzo wielka wina.", "",
        "Przeto błagam Najświętszą Maryję, zawsze Dziewicę,", "wszystkich Aniołów i Świętych,",
        "i was, bracia i siostry,", "o modlitwę za mnie do Pana Boga naszego.", 0 } },
    { "Wieczny odpoczynek", {
        "Wieczny odpoczynek racz im dać, Panie,", "a światłość wiekuista niechaj im świeci.",
        "Niech odpoczywają w pokoju wiecznym.", "Amen.", 0 } },
};
#define MODLITWY_N ((int)(sizeof modlitwy / sizeof modlitwy[0]))

static void cmd_modlitwa(char *arg) {
    int ok = 0, n = arg ? k_atoi(arg, &ok) : 0;
    if (!ok || n < 1 || n > MODLITWY_N) {
        vga_color(ZOLTY, CZARNY); kprintf("\n  Modlitwy:\n");
        for (int i = 0; i < MODLITWY_N; i++) {
            vga_color(BIALY, CZARNY); kprintf("   %d. ", i + 1);
            vga_color(SZARY, CZARNY); kprintf("%s\n", modlitwy[i].tytul);
        }
        vga_color(CIEMNOSZARY, CZARNY); kprintf("\n  Wyświetl: modlitwa NUMER  (np. modlitwa 1)\n\n");
        vga_color(SZARY, CZARNY);
        return;
    }
    const modlitwa_t *mo = &modlitwy[n - 1];
    plansza(mo->tytul, ATTR(BIALY, NIEBIESKI), ATTR(ZOLTY, NIEBIESKI));
    int ile = 0; while (mo->linie[ile]) ile++;
    int y = (VGA_H - ile) / 2;
    for (int i = 0; i < ile; i++) wysrodkuj(y + i, mo->linie[i], ATTR(BIALY, NIEBIESKI));
    wysrodkuj(VGA_H - 2, "dowolny klawisz - powrót", ATTR(JASNONIEBIESKI, NIEBIESKI));
    czekaj_i_wroc();
}

/* ====================================================================== */
/*  Tablica pieśni                                                         */
/* ====================================================================== */

static void pokaz_piesni(void) {
    czas_t c; rtc_czytaj(&c);
    dzien_lit_t l; liturgia(c.rok, c.mies, c.dzien, &l);
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', ATTR(BIALY, CZARNY));
    u8 pasek = ATTR(kolor_tekst(l.kolor), kolor_vga(l.kolor));
    vga_fill(0, 0, VGA_W, 1, ' ', pasek);
    wysrodkuj(0, l.nazwa, pasek);

    int ile = 0;
    for (int i = 0; i < PIESNI_N; i++) if (piesni[i][0]) ile++;
    int krok = ile ? (VGA_H - 2) / ile : 4;
    if (krok > 6) krok = 6;
    if (krok < 4) krok = 4;
    int y = 2 + ((VGA_H - 2) - krok * ile + (krok - 4)) / 2;
    if (y < 1) y = 1;
    ile = 0;
    for (int i = 0; i < PIESNI_N; i++) {
        if (!piesni[i][0]) continue;
        if (y + 4 > VGA_H) break;
        vga_puts_at(3, y + 1, piesni_etyk[i], ATTR(ZOLTY, CZARNY));
        int len = (int)k_strlen(piesni[i]);
        int x = VGA_W - 3 - (len * 12 - 2);
        for (int k = 0; k < len; k++) duzy_znak(x + k * 12, y, piesni[i][k] - '0', 0, ATTR(BIALY, CZARNY));
        y += krok; ile++;
    }
    if (!ile) wysrodkuj(12, "Brak pieśni. Wpisz np.: piesni 101 230 45 12 9", ATTR(SZARY, CZARNY));
    czekaj_i_wroc();
}

static void zapisz_numer(int i, const char *s) {
    int k = 0;
    for (; *s && k < 4; s++) if (*s >= '0' && *s <= '9') piesni[i][k++] = *s;
    piesni[i][k] = 0;
}

static void cmd_piesni(char *p) {
    char *a = k_token(&p);
    char f[16];
    if (a) k_fold_pl(f, a, sizeof f);
    if (a && !k_strcmp(f, "czysc")) { memset(piesni, 0, sizeof piesni); kprintf("Tablica pieśni wyczyszczona.\n"); return; }
    if (a && !k_strcmp(f, "pokaz")) { pokaz_piesni(); return; }
    if (a) {
        memset(piesni, 0, sizeof piesni);
        for (int i = 0; a && i < PIESNI_N; i++, a = k_token(&p)) zapisz_numer(i, a);
        pokaz_piesni();
        return;
    }
    vga_color(ZOLTY, CZARNY);
    kprintf("Podaj numery pieśni (Enter = pomiń, maks. 4 cyfry):\n");
    for (int i = 0; i < PIESNI_N; i++) {
        char buf[16];
        vga_color(SZARY, CZARNY); kprintf("  %-20s: ", piesni_etyk[i]);
        vga_color(BIALY, CZARNY);
        kbd_readline(buf, sizeof buf);
        zapisz_numer(i, buf);
    }
    vga_color(SZARY, CZARNY);
    pokaz_piesni();
}

/* ====================================================================== */
/*  Zegar, msze, ogłoszenia                                                */
/* ====================================================================== */

static void cmd_zegar(void) {
    vga_cursor(0);
    int ost = -1;
    for (;;) {
        if (kbd_poll() >= 0) break;
        czas_t c; rtc_czytaj(&c);
        if (c.sek == ost) continue;
        ost = c.sek;
        dzien_lit_t l; liturgia(c.rok, c.mies, c.dzien, &l);
        vga_fill(0, 0, VGA_W, VGA_H, ' ', ATTR(BIALY, CZARNY));
        wysrodkuj(1, parafia, ATTR(SZARY, CZARNY));
        int zn[5] = { c.godz / 10, c.godz % 10, 10, c.min / 10, c.min % 10 };
        int szer = 0; for (int i = 0; i < 5; i++) szer += szer_znaku(zn[i]) + 2;
        int x = (VGA_W - szer + 2) / 2;
        for (int i = 0; i < 5; i++) {
            if (!(i == 2 && (c.sek & 1))) duzy_znak(x, 4, zn[i], 1, ATTR(BIALY, CZARNY));
            x += szer_znaku(zn[i]) + 2;
        }
        char t[96];
        ksprintf(t, sizeof t, ":%02d", c.sek);
        vga_puts_at(x - 1, 10, t, ATTR(CIEMNOSZARY, CZARNY));
        ksprintf(t, sizeof t, "%s, %d %s %d", dni_tyg_wielk[dzien_tygodnia(dni_od_epoki(c.rok, c.mies, c.dzien))],
                 c.dzien, mies_dop[c.mies], c.rok);
        wysrodkuj(13, t, ATTR(ZOLTY, CZARNY));
        wysrodkuj(15, l.nazwa, ATTR(BIALY, CZARNY));
        if (l.uwaga[0]) { ksprintf(t, sizeof t, "(%s)", l.uwaga); wysrodkuj(16, t, ATTR(SZARY, CZARNY)); }
        ksprintf(t, sizeof t, "%s  ·  kolor %s", l.okres, kolor_nazwa(l.kolor));
        wysrodkuj(18, t, ATTR(SZARY, CZARNY));
        vga_fill(0, 22, VGA_W, 3, ' ', ATTR(BIALY, kolor_vga(l.kolor)));
    }
    vga_color(SZARY, CZARNY); vga_clear(); vga_cursor(1);
}

static void pokaz_msze(void) {
    plansza("PORZĄDEK MSZY ŚWIĘTYCH", ATTR(BIALY, NIEBIESKI), ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj(2, parafia, ATTR(JASNOTURKUSOWY, NIEBIESKI));
    int y = 6;
    for (int i = 0; i < MSZE_N; i++) {
        if (!msze[i].etyk[0]) continue;
        vga_puts_at(6, y, msze[i].etyk, ATTR(ZOLTY, NIEBIESKI));
        y += zawin(msze[i].godz, 10, y + 1, 64, ATTR(BIALY, NIEBIESKI)) + 2;
    }
    wysrodkuj(VGA_H - 2, "dowolny klawisz - powrót", ATTR(JASNONIEBIESKI, NIEBIESKI));
    czekaj_i_wroc();
}

static void cmd_msze(char *p) {
    char *a = k_token(&p);
    char f[16];
    if (!a) { pokaz_msze(); return; }
    k_fold_pl(f, a, sizeof f);
    if (k_strcmp(f, "ustaw")) { kprintf("Użycie: msze        - pokaż porządek Mszy\n        msze ustaw  - zmień godziny\n"); return; }
    vga_color(ZOLTY, CZARNY);
    kprintf("Wpisz nowe godziny (Enter = bez zmian):\n");
    for (int i = 0; i < MSZE_N; i++) {
        char buf[64];
        vga_color(SZARY, CZARNY); kprintf("  %s [%s]\n  > ", msze[i].etyk, msze[i].godz);
        vga_color(BIALY, CZARNY);
        if (kbd_readline(buf, sizeof buf) > 0) k_strncpy(msze[i].godz, buf, sizeof msze[i].godz);
    }
    vga_color(SZARY, CZARNY);
    kprintf("Zapisano. Pokaż: msze\n");
}

static void pokaz_ogloszenia(void) {
    plansza("OGŁOSZENIA PARAFIALNE", ATTR(CZARNY, BIALY), ATTR(CZERWONY, BIALY));
    wysrodkuj(2, parafia, ATTR(CIEMNOSZARY, BIALY));
    int y = 4;
    for (int i = 0; i < ogl_n && y < VGA_H - 3; i++) {
        char nr[8]; ksprintf(nr, sizeof nr, "%d.", i + 1);
        vga_puts_at(3, y, nr, ATTR(CZERWONY, BIALY));
        y += zawin(ogloszenia[i], 7, y, 70, ATTR(CZARNY, BIALY)) + 1;
    }
    if (!ogl_n) wysrodkuj(12, "Brak ogłoszeń.", ATTR(CIEMNOSZARY, BIALY));
    wysrodkuj(VGA_H - 2, "dowolny klawisz - powrót", ATTR(CIEMNOSZARY, BIALY));
    czekaj_i_wroc();
}

static void cmd_ogloszenia(char *p) {
    char *a = k_token(&p);
    char f[16];
    if (!a) { pokaz_ogloszenia(); return; }
    k_fold_pl(f, a, sizeof f);
    if (!k_strcmp(f, "dodaj")) {
        while (*p == ' ') p++;
        if (!*p) { kprintf("Użycie: ogloszenia dodaj <treść ogłoszenia>\n"); return; }
        if (ogl_n >= OGL_N) { kprintf("Za dużo ogłoszeń (maks. %d). Usuń któreś.\n", OGL_N); return; }
        k_strncpy(ogloszenia[ogl_n++], p, sizeof ogloszenia[0]);
        kprintf("Dodano ogłoszenie nr %d.\n", ogl_n);
    } else if (!k_strcmp(f, "usun")) {
        char *nr = k_token(&p); int ok = 0, n = nr ? k_atoi(nr, &ok) : 0;
        if (!ok || n < 1 || n > ogl_n) { kprintf("Użycie: ogloszenia usun NUMER\n"); return; }
        for (int i = n - 1; i < ogl_n - 1; i++) k_strncpy(ogloszenia[i], ogloszenia[i + 1], sizeof ogloszenia[0]);
        ogl_n--;
        kprintf("Usunięto ogłoszenie nr %d.\n", n);
    } else if (!k_strcmp(f, "czysc")) {
        ogl_n = 0; kprintf("Wszystkie ogłoszenia usunięte.\n");
    } else if (!k_strcmp(f, "lista")) {
        for (int i = 0; i < ogl_n; i++) kprintf("  %d. %s\n", i + 1, ogloszenia[i]);
        if (!ogl_n) kprintf("  (brak ogłoszeń)\n");
    } else {
        kprintf("Użycie: ogloszenia [dodaj <tekst> | usun N | lista | czysc]\n");
    }
}

/* ====================================================================== */
/*  Muzyka mszalna                                                         */
/* ====================================================================== */

typedef struct { const char *tytul, *czesc, *opis; const u8 *nuty; const u16 *czasy; int n; } utwor_t;
#include "muzyka_dane.h"

static const char *nazwy_nut[12] = { "C", "Cis", "D", "Dis", "E", "F", "Fis", "G", "Gis", "A", "B", "H" };

static int czas_utworu(const utwor_t *u) { int t = 0; for (int i = 0; i < u->n; i++) t += u->czasy[i]; return t; }

static int utwor_pasuje_do_dnia(const utwor_t *u, const dzien_lit_t *l) {
    const char *c = u->czesc;
    if (!k_strcmp(c, "Adwent")) return !k_strcmp(l->okres, "Adwent");
    if (!k_strcmp(c, "Boże Narodzenie")) return !k_strcmp(l->okres, "Okres Bożego Narodzenia");
    if (!k_strcmp(c, "Wielki Post")) return !k_strcmp(l->okres, "Wielki Post") || !k_strcmp(l->okres, "Wielki Tydzień");
    return 1;
}

/* rysuje "pianolę": nadchodzące nuty jako paski, wysokość = wysokość dźwięku */
static void rysuj_pianole(const utwor_t *u, int i, int lo, int hi) {
    u8 tlo = ATTR(BIALY, CZARNY);
    vga_fill(1, 7, VGA_W - 2, 13, ' ', tlo);
    for (int y = 7; y < 20; y++) vga_glyph_at(6, y, 0xB3, ATTR(CIEMNOSZARY, CZARNY));
    int x = 7;
    for (int k = i; k < u->n && x < VGA_W - 2; k++) {
        int dl = u->czasy[k] / 90; if (dl < 1) dl = 1;
        if (u->nuty[k]) {
            int y = 19 - (hi > lo ? (u->nuty[k] - lo) * 12 / (hi - lo) : 6);
            u8 a = (k == i) ? ATTR(ZOLTY, CZARNY) : ATTR(JASNOTURKUSOWY, CZARNY);
            for (int j = 0; j < dl - 1 && x + j < VGA_W - 2; j++) vga_glyph_at(x + j, y, 0xDB, a);
        }
        x += dl;
    }
    char t[16];
    if (u->nuty[i]) ksprintf(t, sizeof t, " %s%d ", nazwy_nut[u->nuty[i] % 12], u->nuty[i] / 12 - 1);
    else k_strncpy(t, "       ", sizeof t);
    vga_puts_at(2, 7, "       ", tlo);
    vga_puts_at(1, 7, t, ATTR(ZOLTY, CZARNY));
}

/* gra utwór z ekranem; 0 = koniec, 1 = następny, -1 = stop */
static int graj_utwor(int idx, int nr, int z_ilu) {
    const utwor_t *u = &utwory[idx];
    int lo = 127, hi = 0, calosc = czas_utworu(u), minelo = 0;
    for (int i = 0; i < u->n; i++) if (u->nuty[i]) { if (u->nuty[i] < lo) lo = u->nuty[i]; if (u->nuty[i] > hi) hi = u->nuty[i]; }

    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', ATTR(BIALY, CZARNY));
    ramka(0, 0, VGA_W, VGA_H, ATTR(BRAZOWY, CZARNY));
    wysrodkuj(0, " MUZYKA MSZALNA ", ATTR(ZOLTY, CZARNY));
    char t[96];
    ksprintf(t, sizeof t, "♫  %s  ♫", u->tytul);
    wysrodkuj(2, t, ATTR(BIALY, CZARNY));
    wysrodkuj(3, u->opis, ATTR(SZARY, CZARNY));
    if (z_ilu > 1) ksprintf(t, sizeof t, "%s  ·  utwór %d z %d", u->czesc, nr, z_ilu);
    else k_strncpy(t, u->czesc, sizeof t);
    wysrodkuj(5, t, ATTR(JASNOTURKUSOWY, CZARNY));
    wysrodkuj(23, " spacja - pauza  ·  → - następny  ·  Esc - stop ", ATTR(CIEMNOSZARY, CZARNY));

    int wynik = 0;
    for (int i = 0; i < u->n && !wynik; i++) {
        rysuj_pianole(u, i, lo, hi);
        /* pasek postępu */
        int w = 60, pel = calosc ? minelo * w / calosc : 0;
        for (int k = 0; k < w; k++) vga_glyph_at(10 + k, 21, k < pel ? 0xDB : 0xB0, ATTR(BRAZOWY, CZARNY));
        ksprintf(t, sizeof t, "%d:%02d / %d:%02d", minelo / 60000, (minelo / 1000) % 60, calosc / 60000, (calosc / 1000) % 60);
        vga_puts_at(10 + w + 2, 21, t, ATTR(SZARY, CZARNY));

        u32 dl = u->czasy[i], graj = u->nuty[i] ? dl * 9 / 10 : 0;
        if (u->nuty[i]) glosnik_ton(czestotliwosc[u->nuty[i]]);
        u32 t0 = zegar_ms();
        for (;;) {
            u32 e = zegar_ms() - t0;
            if (e >= dl) break;
            if (graj && e >= graj) { glosnik_cisza(); graj = 0; }
            int k = kbd_poll();
            if (k == KEY_ESC || k == 'q') { wynik = -1; break; }
            if (k == KEY_RIGHT || k == KEY_ENTER) { wynik = 1; break; }
            if (k == ' ') {
                glosnik_cisza();
                wysrodkuj(19, "  PAUZA  ", ATTR(CZARNY, ZOLTY));
                int k2; do k2 = kbd_getkey(); while (k2 != ' ' && k2 != KEY_ESC);
                if (k2 == KEY_ESC) { wynik = -1; break; }
                if (u->nuty[i] && graj) glosnik_ton(czestotliwosc[u->nuty[i]]);
                t0 = zegar_ms() - e;
            }
        }
        glosnik_cisza();
        minelo += (int)dl;
    }
    glosnik_cisza();
    return wynik;
}

static void cmd_muzyka(char *arg) {
    if (arg) {
        char f[16]; k_fold_pl(f, arg, sizeof f);
        int ok, n = k_atoi(arg, &ok);
        if (!k_strcmp(f, "wszystko")) {
            for (int i = 0; i < UTWORY_N; i++) if (graj_utwor(i, i + 1, UTWORY_N) < 0) break;
        } else if (ok && n >= 1 && n <= UTWORY_N) {
            graj_utwor(n - 1, 1, 1);
        } else { kprintf("Użycie: muzyka [NUMER | wszystko]\n"); return; }
        vga_color(SZARY, CZARNY); vga_clear(); vga_cursor(1);
        return;
    }
    czas_t c; rtc_czytaj(&c);
    dzien_lit_t l; liturgia(c.rok, c.mies, c.dzien, &l);
    vga_color(ZOLTY, CZARNY); kprintf("\n  Muzyka mszalna  ♫\n");
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("  ────────────────────────────────────────────────────────────────────────\n");
    for (int i = 0; i < UTWORY_N; i++) {
        const utwor_t *u = &utwory[i];
        int s = czas_utworu(u) / 1000, pas = utwor_pasuje_do_dnia(u, &l);
        vga_color(BIALY, CZARNY);      kprintf("  %d. ", i + 1);
        vga_color(pas ? BIALY : CIEMNOSZARY, CZARNY); kprintf("%-34s", u->tytul);
        vga_color(pas ? JASNOTURKUSOWY : CIEMNOSZARY, CZARNY); kprintf("%-26s", u->czesc);
        vga_color(CIEMNOSZARY, CZARNY); kprintf("%d:%02d\n", s / 60, s % 60);
    }
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("\n  Jasne pozycje pasują do okresu: %s.\n", l.okres);
    kprintf("  Odtwórz: muzyka NUMER   lub   muzyka wszystko\n\n");
    vga_color(SZARY, CZARNY);
}

/* ====================================================================== */
/*  Komunia święta                                                         */
/* ====================================================================== */

static u8 plotno[50][80];               /* "piksele": 80 x 50 (2 na znak w pionie) */
static const short kierunki[32][2] = { {1000,0}, {981,195}, {924,383}, {831,556}, {707,707}, {556,831}, {383,924}, {195,981}, {0,1000}, {-195,981}, {-383,924}, {-556,831}, {-707,707}, {-831,556}, {-924,383}, {-981,195}, {-1000,0}, {-981,-195}, {-924,-383}, {-831,-556}, {-707,-707}, {-556,-831}, {-383,-924}, {-195,-981}, {0,-1000}, {195,-981}, {383,-924}, {556,-831}, {707,-707}, {831,-556}, {924,-383}, {981,-195} };

static void plotno_px(int x, int y, u8 c) { if (x >= 0 && x < 80 && y >= 0 && y < 50) plotno[y][x] = c; }

static void plotno_pokaz(int wiersze) {
    for (int r = 0; r < wiersze; r++)
        for (int x = 0; x < 80; x++) {
            u8 g = plotno[2 * r][x], d = plotno[2 * r + 1][x];
            if (g == d) vga_glyph_at(x, r, ' ', ATTR(g, g));
            else vga_glyph_at(x, r, 0xDF, ATTR(g, d));
        }
}

/* Hostia w promieniach, jak w monstrancji */
static void rysuj_hostie(int blask) {
    int cx = 40, cy = 17, r = 9;
    memset(plotno, NIEBIESKI, sizeof plotno);
    for (int k = 0; k < 32; k++) {
        int dlugi = (k % 2 == 0), od = r + 3, doo = r + (dlugi ? 8 + blask : 5 + blask);
        for (int d = od * 10; d <= doo * 10; d += 5)
            plotno_px(cx + kierunki[k][0] * d / 10000, cy + kierunki[k][1] * d / 10000, dlugi ? ZOLTY : BRAZOWY);
    }
    for (int y = -r - 1; y <= r + 1; y++)
        for (int x = -r - 1; x <= r + 1; x++) {
            int d2 = x * x + y * y;
            if (d2 <= r * r) plotno_px(cx + x, cy + y, d2 >= (r - 1) * (r - 1) ? SZARY : BIALY);
            else if (d2 <= (r + 1) * (r + 1)) plotno_px(cx + x, cy + y, ZOLTY);
        }
    for (int y = -6; y <= 6; y++) { plotno_px(cx, cy + y, SZARY); plotno_px(cx - 1, cy + y, SZARY); }
    for (int x = -4; x <= 3; x++) plotno_px(cx + x, cy - 2, SZARY);
    plotno_pokaz(18);
}

static void wysrodkuj_zawin(int y, const char *s, int szer, u8 a) {
    u32 cp[400]; int n = 0;
    while (*s && n < 400) cp[n++] = k_utf8_next(&s);
    int i = 0;
    while (i < n) {
        int koniec = i + szer;
        if (koniec >= n) koniec = n;
        else { int k = koniec; while (k > i && cp[k] != ' ') k--; if (k > i) koniec = k; }
        int x = (VGA_W - (koniec - i)) / 2;
        for (int j = i; j < koniec; j++) vga_glyph_at(x + j - i, y, vga_map(cp[j]), a);
        y++;
        i = koniec;
        while (i < n && cp[i] == ' ') i++;
    }
}

static void dzwonki(void) {
    for (int seria = 0; seria < 3; seria++) {
        u32 t0 = zegar_ms(); int przel = 0;
        while (zegar_ms() - t0 < 450) {
            glosnik_ton(przel ? 2093 : 2637);
            czekaj_ms(28);
            przel = !przel;
        }
        glosnik_cisza();
        czekaj_ms(220);
    }
}

/* ekran z Hostią: słowa kapłana i odpowiedź wiernego; -1 = Esc */
static int komunia_krok(const char *kaplan, const char *podpowiedz, const char *odpowiedz, int blask) {
    rysuj_hostie(blask);
    vga_fill(0, 18, VGA_W, 7, ' ', ATTR(BIALY, NIEBIESKI));
    wysrodkuj_zawin(19, kaplan, 70, ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj(22, podpowiedz, ATTR(JASNONIEBIESKI, NIEBIESKI));
    char buf[160];
    vga_color(BIALY, NIEBIESKI);
    vga_goto(2, 23); vga_cursor(1);
    kprintf("Ty: ");
    kbd_historia = 0;
    int r = kbd_readline(buf, sizeof buf);
    kbd_historia = 1;
    vga_cursor(0);
    if (r < 0) return -1;
    vga_fill(0, 22, VGA_W, 3, ' ', ATTR(BIALY, NIEBIESKI));
    wysrodkuj_zawin(22, buf[0] ? buf : odpowiedz, 76, ATTR(BIALY, NIEBIESKI));
    czekaj_ms(1500);
    return 0;
}

static void cmd_komunia(void) {
    if (komunia_krok("Oto Baranek Boży, który gładzi grzechy świata. "
                     "Błogosławieni, którzy zostali wezwani na Jego ucztę.",
                     "Odpowiedz: Panie, nie jestem godzien...   (Enter - wpisze za ciebie)",
                     "Panie, nie jestem godzien, abyś przyszedł do mnie, ale powiedz tylko słowo, "
                     "a będzie uzdrowiona dusza moja.", 0) < 0) goto koniec;
    rysuj_hostie(2);
    vga_fill(0, 18, VGA_W, 7, ' ', ATTR(BIALY, NIEBIESKI));
    dzwonki();
    if (komunia_krok("Ciało Chrystusa.", "Odpowiedz: Amen.", "Amen.", 2) < 0) goto koniec;

    /* dziękczynienie z muzyką */
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', ATTR(BIALY, NIEBIESKI));
    ramka(0, 0, VGA_W, VGA_H, ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj(0, " DZIĘKCZYNIENIE PO KOMUNII ŚWIĘTEJ ", ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj(4, "Akt komunii duchowej", ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj_zawin(7, "Jezu mój, wierzę, że jesteś obecny w Najświętszym Sakramencie. "
                       "Kocham Cię nade wszystko i pragnę Cię przyjąć do mojej duszy. "
                       "Ponieważ teraz nie mogę Cię przyjąć sakramentalnie, "
                       "przyjdź przynajmniej duchowo do mego serca. "
                       "Obejmuję Cię i jednoczę się z Tobą. "
                       "Nie dozwól, abym kiedykolwiek miał się od Ciebie odłączyć. Amen.",
                    58, ATTR(BIALY, NIEBIESKI));
    wysrodkuj(17, "♫  Schmücke dich, o liebe Seele - J. S. Bach  ♫", ATTR(JASNOTURKUSOWY, NIEBIESKI));
    wysrodkuj(VGA_H - 2, "dowolny klawisz - zakończ", ATTR(JASNONIEBIESKI, NIEBIESKI));
    {
        const utwor_t *u = &utwory[5];
        int stop = 0;
        for (int i = 0; i < u->n && !stop; i++) {
            u32 dl = u->czasy[i], graj = u->nuty[i] ? dl * 9 / 10 : 0;
            if (u->nuty[i]) glosnik_ton(czestotliwosc[u->nuty[i]]);
            u32 t0 = zegar_ms();
            while (zegar_ms() - t0 < dl) {
                if (graj && zegar_ms() - t0 >= graj) { glosnik_cisza(); graj = 0; }
                if (kbd_poll() >= 0) { stop = 1; break; }
            }
            glosnik_cisza();
        }
        if (!stop) kbd_getkey();
    }
koniec:
    glosnik_cisza();
    kbd_historia = 1;
    vga_color(SZARY, CZARNY); vga_clear(); vga_cursor(1);
}

/* ====================================================================== */
/*  Spowiedź - wirtualny spowiednik                                        */
/* ====================================================================== */

#define SP_LINIE 160
#define SP_SZER  68                     /* szerokość tekstu za etykietą */

/* Wszystko, co wierny napisze, trzymamy tylko tutaj - i wymazujemy na końcu. */
static struct {
    char t[SP_LINIE][240];              /* zapis rozmowy (zawinięte wiersze) */
    u8   a[SP_LINIE];
    int  n;
    char wej[152];                      /* bieżąca linia wpisywana przez wiernego */
    char male[160];                     /* ta sama linia: małe litery, bez ogonków */
    int  tematy[11], kolejnosc[11], ile_tematow;
    int  grzechow;
} sp;

static const u8 SP_TLO = ATTR(BIALY, NIEBIESKI);
static const u8 SP_KAPLAN = ATTR(ZOLTY, NIEBIESKI);
static const u8 SP_TY = ATTR(BIALY, NIEBIESKI);
static const u8 SP_FORMULA = ATTR(JASNOTURKUSOWY, NIEBIESKI);

static const char *AKT_ZALU =
    "Ach, żałuję za me złości jedynie dla Ciebie, Boże, bom obraził Twą dobroć niezmierzoną. "
    "Ach, żałuję i poprawę obiecuję, dopomóż mi, Panie Boże. Amen.";
static const char *ROZGRZESZENIE =
    "Bóg, Ojciec miłosierdzia, który pojednał świat ze sobą przez śmierć i zmartwychwstanie "
    "swojego Syna i zesłał Ducha Świętego na odpuszczenie grzechów, niech ci udzieli przebaczenia "
    "i pokoju przez posługę Kościoła. I ja odpuszczam tobie grzechy w imię Ojca i Syna, "
    "i Ducha Świętego. ┼";

/* tematy rachunku sumienia: słowa-klucze (bez ogonków), nauka, praktyczna pokuta */
static const struct { const char *klucze[12]; const char *nauka; const char *pokuta; } tematy[11] = {
    { { "modli", "pacierz", "wrozb", "horoskop", "zabobon", "wiar", 0 },
      "Pamiętaj o codziennej modlitwie - to rozmowa z Bogiem, który zawsze na ciebie czeka.",
      "Każdego dnia przez najbliższy tydzień znajdź chwilę na osobistą modlitwę." },
    { { "przeklin", "klal", "klne", "klela", "bluzn", "przysieg", 0 },
      "Szanuj imię Boga i pilnuj swoich słów - słowa mają wielką moc, mogą ranić albo leczyć.",
      "Gdy kusi cię, by zakląć, powiedz w zamian w myślach: Jezu, ufam Tobie." },
    { { "msz", "niedziel", "koscio", "swieto", 0 },
      "Niedzielna Eucharystia to spotkanie, w którym sam Chrystus daje ci siłę na cały tydzień.",
      "W najbliższą niedzielę przyjdź na Mszę świętą trochę wcześniej i pomódl się przed nią." },
    { { "rodzic", "mama", "mamie", "mame", "mamy", "tata", "tacie", "tate", "taty", "matk", "ojc", "posluszn" },
      "Okazuj rodzicom szacunek i wdzięczność - przez nich Bóg pierwszy okazał ci miłość.",
      "Zrób dziś coś dobrego dla swoich rodziców, o co cię nie prosili." },
    { { "klocil", "klotn", "bil", "bije", "zlosc", "zlosci", "gniew", "nienawi", "przezyw", "krzywd", "dokucz", "zemst" },
      "Gniew zamień na przebaczenie - Jezus uczy nas kochać nawet tych, którzy nas zranili.",
      "Przeproś tych, których zraniły twoje słowa lub czyny." },
    { { "nieczyst", "czystos", "zdradz", "zdrad", "porno", "cudzolo", "pozadl", 0 },
      "Dbaj o czystość serca, myśli i spojrzeń - twoje ciało jest świątynią Ducha Świętego.",
      "Unikaj okazji do grzechu i pomódl się za swoją rodzinę." },
    { { "ukradl", "kradl", "kradz", "oszuk", "sciag", "zniszczy", 0 },
      "Bądź uczciwy także w drobnych sprawach - kto jest wierny w małym, będzie wierny i w wielkim.",
      "Oddaj lub napraw to, co nie należy do ciebie." },
    { { "klam", "oklam", "obmaw", "plotk", "oczern", "nieprawd", 0 },
      "Prawda wyzwala - staraj się mówić prawdę, nawet wtedy, gdy jest trudna.",
      "Jeśli to możliwe, napraw skutki kłamstwa lub obmowy i powiedz o kimś coś dobrego." },
    { { "zazdro", "chciw", "zawisc", "zawisc", 0 },
      "Ciesz się z tego, co masz, i z sukcesów innych - wdzięczność leczy serce z zazdrości.",
      "Podziękuj dziś Bogu za trzy dobre rzeczy w twoim życiu." },
    { { "lenis", "leniw", "lekcj", "obowiazk", "spozni", 0 },
      "Sumiennie wypełniaj swoje obowiązki - w nich także służysz Bogu i ludziom.",
      "Wykonaj jutro jeden zaległy obowiązek bez odkładania." },
    { { "alkohol", "upil", "pij", "papieros", "narkot", "hazard", 0 },
      "Troszcz się o swoje zdrowie - to dar od Boga, za który jesteś odpowiedzialny przed Nim.",
      "Przez tydzień zrezygnuj z czegoś, co ci szkodzi, i ofiaruj to Bogu." },
};

static int zawiera(const char *h, const char *n) {
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static void pauza(int ms) { czekaj_ms((u32)ms); }

static void sp_dodaj_wiersz(const char *s, u8 a) {
    if (sp.n == SP_LINIE) {
        for (int i = 0; i < SP_LINIE - 1; i++) { memcpy(sp.t[i], sp.t[i + 1], sizeof sp.t[0]); sp.a[i] = sp.a[i + 1]; }
        sp.n--;
    }
    k_strncpy(sp.t[sp.n], s, sizeof sp.t[0]);
    sp.a[sp.n++] = a;
}

/* dopisuje wypowiedź do rozmowy, zawijając ją po słowach */
static void sp_wpis(int kaplan, const char *txt, u8 a) {
    u32 cp[600]; int n = 0;
    while (*txt && n < 600) cp[n++] = k_utf8_next(&txt);
    int i = 0, pierwszy = 1;
    char w[240];
    do {
        int koniec = i + SP_SZER;
        if (koniec >= n) koniec = n;
        else { int k = koniec; while (k > i && cp[k] != ' ') k--; if (k > i) koniec = k; }
        int len = 0;
        const char *pref = pierwszy ? (kaplan ? "Kapłan: " : "Ty:     ") : "        ";
        while (*pref) w[len++] = *pref++;
        for (int j = i; j < koniec; j++) len += k_utf8_put(cp[j], w + len);
        w[len] = 0;
        sp_dodaj_wiersz(w, a);
        pierwszy = 0;
        i = koniec;
        while (i < n && cp[i] == ' ') i++;
    } while (i < n);
    sp_dodaj_wiersz("", SP_TLO);
}

static void sp_rysuj(const char *podpowiedz) {
    vga_fill(0, 0, VGA_W, VGA_H, ' ', SP_TLO);
    vga_fill(0, 0, VGA_W, 1, ' ', ATTR(NIEBIESKI, ZOLTY));
    wysrodkuj(0, "SPOWIEDŹ ŚWIĘTA", ATTR(NIEBIESKI, ZOLTY));
    int wiersze = 18, start = sp.n > wiersze ? sp.n - wiersze : 0;
    for (int i = start; i < sp.n; i++) vga_puts_at(2, 2 + i - start, sp.t[i], sp.a[i]);
    vga_fill(0, 21, VGA_W, 1, 0xC4, ATTR(NIEBIESKI, NIEBIESKI));
    if (podpowiedz) vga_puts_at(2, 21, podpowiedz, ATTR(JASNONIEBIESKI, NIEBIESKI));
    vga_fill(0, 24, VGA_W, 1, ' ', ATTR(SZARY, CZARNY));
    vga_puts_at(1, 24, "F10 - przerwij i wymaż", ATTR(SZARY, CZARNY));
}

static void sp_kaplan(const char *txt, u8 a) {
    sp_dodaj_wiersz("Kapłan: ...", ATTR(JASNONIEBIESKI, NIEBIESKI));
    sp_rysuj(0);
    vga_cursor(0);
    pauza(700 + (int)k_utf8_len(txt) * 6);
    sp.n--;
    sp_wpis(1, txt, a);
}

/* wierny pisze; pusta linia = domyślna odpowiedź (jeśli jest); -1 = F10 */
static int sp_pytaj(const char *podpowiedz, const char *domyslna) {
    sp_rysuj(podpowiedz);
    vga_color(BIALY, NIEBIESKI);
    vga_goto(2, 22);
    vga_cursor(1);
    kprintf("Ty: ");
    if (kbd_readline(sp.wej, sizeof sp.wej) < 0) return -1;
    if (sp.wej[0]) sp_wpis(0, sp.wej, SP_TY);
    else if (domyslna) sp_wpis(0, domyslna, SP_TY);
    k_fold_pl(sp.male, sp.wej, sizeof sp.male);
    return 0;
}

static void sp_rozpoznaj(const char *male) {
    for (int t = 0; t < 11; t++)
        for (int k = 0; k < 12 && tematy[t].klucze[k]; k++)
            if (zawiera(male, tematy[t].klucze[k])) {
                if (!sp.tematy[t]) { sp.tematy[t] = 1; sp.kolejnosc[sp.ile_tematow++] = t; }
                break;
            }
}

static void sp_wymaz(void) {
    volatile u8 *p = (volatile u8 *)&sp;
    for (u32 i = 0; i < sizeof sp; i++) p[i] = 0;
    kbd_historia = 1;
    vga_color(SZARY, CZARNY);
    vga_clear();
}

static void cmd_spowiedz(void) {
    /* ekran wstępny */
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', SP_TLO);
    duzy_napis(3, "SPOWIEDŹ", ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj(11, "Wirtualny spowiednik AmenOS", ATTR(BIALY, NIEBIESKI));
    wysrodkuj(13, "Rozmowa jest prywatna: system nie ma dysku ani sieci,", ATTR(JASNOTURKUSOWY, NIEBIESKI));
    wysrodkuj(14, "a po zakończeniu wszystko zostanie wymazane z pamięci.", ATTR(JASNOTURKUSOWY, NIEBIESKI));
    wysrodkuj(17, "Enter - rozpocznij      Esc - wyjście", ATTR(BIALY, NIEBIESKI));
    for (;;) {
        int k = kbd_getkey();
        if (k == KEY_ESC) { vga_color(SZARY, CZARNY); vga_clear(); vga_cursor(1); return; }
        if (k == KEY_ENTER) break;
    }

    memset(&sp, 0, sizeof sp);
    kbd_historia = 0;                   /* nic z rozmowy nie trafi do historii poleceń */

    if (sp_pytaj("Zacznij od: Niech będzie pochwalony Jezus Chrystus. (Enter - wpisze za ciebie)",
                 "Niech będzie pochwalony Jezus Chrystus.") < 0) goto koniec;
    sp_kaplan("Na wieki wieków. Amen. W imię Ojca i Syna, i Ducha Świętego. "
              "Bóg, który oświeca każde serce, niech ci da prawdziwe poznanie twoich grzechów "
              "i swojego miłosierdzia.", SP_KAPLAN);

    if (sp_pytaj("Powiedz, kiedy była ostatnia spowiedź i czy pokuta została odprawiona.", 0) < 0) goto koniec;
    sp_kaplan("Słucham. Wyznaj swoje grzechy - możesz pisać po kolei, każdy w osobnej linii.", SP_KAPLAN);

    int zachecono = 0;
    for (;;) {
        if (sp_pytaj("Wpisz grzech i Enter. Koniec: \"Więcej grzechów nie pamiętam\" lub pusta linia.", 0) < 0)
            goto koniec;
        if (!sp.wej[0]) {
            if (!sp.grzechow && !zachecono) {
                zachecono = 1;
                sp_kaplan("Nie bój się. Bóg zna twoje serce i czeka na ciebie z miłością. "
                          "Co chcesz Mu powiedzieć?", SP_KAPLAN);
                continue;
            }
            sp_wpis(0, "Więcej grzechów nie pamiętam. Za wszystkie serdecznie żałuję "
                       "i proszę o pokutę i rozgrzeszenie.", SP_TY);
            break;
        }
        if (zawiera(sp.male, "nie pamietam") || zawiera(sp.male, "prosze o pokute")) break;
        sp.grzechow++;
        sp_rozpoznaj(sp.male);
    }

    /* nauka i pokuta dobrane do wyznanych grzechów */
    char nauka[600], pokuta[400];
    int poz = 0;
    ksprintf(nauka, sizeof nauka, "Dziękuję za szczere wyznanie. Bóg cieszy się z każdego, kto do Niego wraca. ");
    poz = (int)k_strlen(nauka);
    for (int i = 0; i < sp.ile_tematow && i < 2; i++) {
        ksprintf(nauka + poz, sizeof nauka - (u32)poz, "%s ", tematy[sp.kolejnosc[i]].nauka);
        poz = (int)k_strlen(nauka);
    }
    ksprintf(nauka + poz, sizeof nauka - (u32)poz,
             "Pamiętaj, że Boże miłosierdzie jest większe niż każdy grzech.");
    sp_kaplan(nauka, SP_KAPLAN);

    const char *modlitwa = sp.grzechow <= 3 ? "jedną dziesiątkę różańca"
                         : sp.grzechow <= 7 ? "Koronkę do Miłosierdzia Bożego"
                         : "jedną część różańca";
    if (sp.ile_tematow)
        ksprintf(pokuta, sizeof pokuta, "Za pokutę odmów %s. %s", modlitwa, tematy[sp.kolejnosc[0]].pokuta);
    else
        ksprintf(pokuta, sizeof pokuta, "Za pokutę odmów %s.", modlitwa);
    sp_kaplan(pokuta, SP_KAPLAN);

    sp_kaplan("Czy żałujesz za swoje grzechy? Odmów teraz akt żalu.", SP_KAPLAN);
    if (sp_pytaj("Ach, żałuję za me złości...  (Enter - wpisze cały akt żalu)", AKT_ZALU) < 0) goto koniec;

    sp_kaplan(ROZGRZESZENIE, SP_FORMULA);
    if (sp_pytaj("Odpowiedz: Amen.", "Amen.") < 0) goto koniec;
    sp_kaplan("Chwalcie Pana, bo jest dobry.", SP_KAPLAN);
    if (sp_pytaj("Odpowiedz: Bo Jego miłosierdzie trwa na wieki.", "Bo Jego miłosierdzie trwa na wieki.") < 0)
        goto koniec;
    sp_kaplan("Pan odpuścił tobie grzechy. Idź w pokoju.", SP_KAPLAN);
    if (sp_pytaj("Odpowiedz: Bóg zapłać.", "Bóg zapłać.") < 0) goto koniec;

    sp_rysuj("Enter - zakończ. Cała rozmowa zostanie wymazana z pamięci.");
    vga_cursor(0);
    while (kbd_getkey() != KEY_ENTER) ;

koniec:
    sp_wymaz();
    memset(nauka, 0, sizeof nauka);
    memset(pokuta, 0, sizeof pokuta);
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', SP_TLO);
    wysrodkuj(11, "Rozmowa została wymazana z pamięci.", ATTR(BIALY, NIEBIESKI));
    wysrodkuj(13, "Szczęść Boże!", ATTR(ZOLTY, NIEBIESKI));
    czekaj_sekundy(2);
    vga_color(SZARY, CZARNY);
    vga_clear();
    vga_cursor(1);
}

/* ====================================================================== */
/*  System                                                                 */
/* ====================================================================== */

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static void cmd_system(void) {
    u32 r[12], a, b, c, d;
    char nazwa[52];
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        for (u32 i = 0; i < 3; i++) cpuid(0x80000002 + i, &r[i * 4], &r[i * 4 + 1], &r[i * 4 + 2], &r[i * 4 + 3]);
        memcpy(nazwa, r, 48); nazwa[48] = 0;
    } else {
        cpuid(0, &a, &b, &c, &d);
        memcpy(nazwa, &b, 4); memcpy(nazwa + 4, &d, 4); memcpy(nazwa + 8, &c, 4); nazwa[12] = 0;
    }
    char *n = nazwa; while (*n == ' ') n++;
    vga_color(ZOLTY, CZARNY);
    kprintf("\n  AmenOS %s - system operacyjny dla parafii\n", AMENOS_WERSJA);
    vga_color(SZARY, CZARNY);
    kprintf("  Parafia:   %s\n", parafia);
    kprintf("  Procesor:  %s\n", n);
    kprintf("  Pamięć:    %u MB\n", (int)(pamiec_kb / 1024));
    kprintf("  Strefa:    zegar sprzętowy %s%d h\n", strefa_godzin >= 0 ? "+" : "", strefa_godzin);
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("  Jądro 32-bitowe x86, start przez GRUB (Multiboot), ekran VGA 80x25.\n");
    kprintf("  Dane (pieśni, ogłoszenia, msze) są w pamięci RAM - znikają po wyłączeniu.\n\n");
    vga_color(SZARY, CZARNY);
}

static void cmd_restart(void) {
    kprintf("Ponowne uruchamianie...\n");
    while (inb(0x64) & 0x02) ;
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("hlt");
}

static void cmd_wylacz(void) {
    outw(0x604, 0x2000);    /* QEMU */
    outw(0xB004, 0x2000);   /* Bochs, starsze QEMU */
    outw(0x4004, 0x3400);   /* VirtualBox */
    vga_color(BIALY, NIEBIESKI);
    vga_clear();
    vga_cursor(0);
    wysrodkuj(11, "Szczęść Boże!", ATTR(ZOLTY, NIEBIESKI));
    wysrodkuj(13, "Można bezpiecznie wyłączyć komputer.", ATTR(BIALY, NIEBIESKI));
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

static void cmd_strefa(char *arg) {
    if (arg) {
        int ok; int v = k_atoi(arg, &ok);
        if (!ok || v < -12 || v > 14) { kprintf("Użycie: strefa N   (np. strefa 1 - zima w Polsce, strefa 2 - lato,\n        gdy zegar komputera chodzi wg UTC)\n"); return; }
        strefa_godzin = v;
    }
    czas_t c; rtc_czytaj(&c);
    kprintf("Przesunięcie: %s%d h. Teraz: %02d:%02d, %d %s %d\n", strefa_godzin >= 0 ? "+" : "",
            strefa_godzin, c.godz, c.min, c.dzien, mies_dop[c.mies], c.rok);
}

static void cmd_pomoc(void) {
    static const char *p[][2] = {
        { "dzis",                 "obchód liturgiczny dnia dzisiejszego" },
        { "dzien DD.MM[.RRRR]",   "obchód w wybranym dniu" },
        { "miesiac [MM [RRRR]]",  "kalendarz miesiąca z kolorami liturgicznymi" },
        { "kalendarz [RRRR]",     "święta ruchome (Wielkanoc, Boże Ciało...)" },
        { "modlitwy / modlitwa N", "modlitwy na ekranie" },
        { "piesni [nr nr ...]",   "tablica pieśni na duży ekran (pokaz / czysc)" },
        { "msze [ustaw]",         "porządek Mszy świętych" },
        { "ogloszenia [dodaj..]", "ogłoszenia parafialne (dodaj/usun/lista/czysc)" },
        { "spowiedz",             "spowiedź święta przy komputerze" },
        { "komunia",              "Komunia święta" },
        { "muzyka [N|wszystko]",  "muzyka mszalna z głośniczka komputera" },
        { "www [adres]",          "przeglądarka internetowa" },
        { "siec / ping ADRES",    "stan sieci (adres IP) / sprawdzenie połączenia" },
        { "zegar",                "duży zegar z obchodem dnia" },
        { "parafia <nazwa>",      "ustaw nazwę parafii" },
        { "czas / strefa N",      "data i godzina / przesunięcie strefy" },
        { "system",               "informacje o systemie i komputerze" },
        { "czysc",                "wyczyść ekran" },
        { "restart / wylacz",     "uruchom ponownie / zakończ pracę" },
    };
    vga_color(ZOLTY, CZARNY);
    kprintf("\n  Polecenia AmenOS  (polskie litery: prawy Alt + a, c, e, l, n, o, s, x, z)\n");
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("  ────────────────────────────────────────────────────────────────────────\n");
    for (u32 i = 0; i < sizeof p / sizeof p[0]; i++) {
        vga_color(BIALY, CZARNY);  kprintf("  %-24s", p[i][0]);
        vga_color(SZARY, CZARNY);  kprintf("%s\n", p[i][1]);
    }
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("  Strzałki góra/dół - historia poleceń, Esc - wyczyść linię, F1 - pomoc\n\n");
    vga_color(SZARY, CZARNY);
}

/* ====================================================================== */
/*  Ekran powitalny i pętla główna                                         */
/* ====================================================================== */

void ekran_powitalny(void) {
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, VGA_H, ' ', ATTR(BIALY, NIEBIESKI));
    u8 krzyz = ATTR(ZOLTY, NIEBIESKI);
    vga_fill(38, 2, 4, 12, 0xDB, krzyz);       /* belka pionowa */
    vga_fill(32, 5, 16, 2, 0xDB, krzyz);       /* belka pozioma */
    wysrodkuj(16, "A M E N  O S", ATTR(BIALY, NIEBIESKI));
    char t[64]; ksprintf(t, sizeof t, "system operacyjny dla parafii · wersja %s", AMENOS_WERSJA);
    wysrodkuj(18, t, ATTR(JASNOTURKUSOWY, NIEBIESKI));
    wysrodkuj(21, "Niech będzie pochwalony Jezus Chrystus", ATTR(ZOLTY, NIEBIESKI));
    czekaj_sekundy(3);
    vga_color(SZARY, CZARNY);
    vga_clear();
    vga_cursor(1);
}

static void baner(void) {
    czas_t c; rtc_czytaj(&c);
    dzien_lit_t l; liturgia(c.rok, c.mies, c.dzien, &l);
    vga_fill(0, 0, VGA_W, 1, ' ', ATTR(CZARNY, ZOLTY));
    vga_puts_at(1, 0, "AmenOS", ATTR(CZARNY, ZOLTY));
    vga_puts_at(9, 0, parafia, ATTR(NIEBIESKI, ZOLTY));
    char t[32]; ksprintf(t, sizeof t, "%02d.%02d.%d %02d:%02d", c.dzien, c.mies, c.rok, c.godz, c.min);
    vga_puts_at(VGA_W - 1 - (int)k_strlen(t), 0, t, ATTR(CZARNY, ZOLTY));
    vga_goto(0, 1);
    vga_color(SZARY, CZARNY);
    kprintf("\n  Dziś: ");
    vga_color(BIALY, CZARNY); kprintf("%s", l.nazwa);
    vga_color(SZARY, CZARNY); kprintf("  ");
    vga_color(kolor_tekst(l.kolor), kolor_vga(l.kolor)); kprintf(" %s ", kolor_nazwa(l.kolor));
    vga_color(CIEMNOSZARY, CZARNY);
    kprintf("\n  Wpisz ");
    vga_color(BIALY, CZARNY); kprintf("pomoc");
    vga_color(CIEMNOSZARY, CZARNY); kprintf(" (lub naciśnij F1), aby zobaczyć listę poleceń.\n\n");
    vga_color(SZARY, CZARNY);
}

void powloka(u32 mem_kb) {
    pamiec_kb = mem_kb;
    baner();
    char linia[128], f[24];
    for (;;) {
        vga_color(ZOLTY, CZARNY); kprintf("amen");
        vga_color(BIALY, CZARNY); kprintf("> ");
        vga_color(SZARY, CZARNY);
        kbd_readline(linia, sizeof linia);
        char *p = linia;
        char *cmd = k_token(&p);
        if (!cmd) continue;
        k_fold_pl(f, cmd, sizeof f);

        if (!k_strcmp(f, "pomoc") || !k_strcmp(f, "help") || !k_strcmp(f, "?")) cmd_pomoc();
        else if (!k_strcmp(f, "dzis")) cmd_dzis();
        else if (!k_strcmp(f, "dzien")) cmd_dzien(k_token(&p));
        else if (!k_strcmp(f, "miesiac")) { char *a = k_token(&p); cmd_miesiac(a, k_token(&p)); }
        else if (!k_strcmp(f, "kalendarz") || !k_strcmp(f, "wielkanoc")) cmd_kalendarz(k_token(&p));
        else if (!k_strcmp(f, "modlitwy") || !k_strcmp(f, "modlitwa")) cmd_modlitwa(k_token(&p));
        else if (!k_strcmp(f, "piesni") || !k_strcmp(f, "tablica")) cmd_piesni(p);
        else if (!k_strcmp(f, "msze") || !k_strcmp(f, "msza")) cmd_msze(p);
        else if (!k_strcmp(f, "ogloszenia")) cmd_ogloszenia(p);
        else if (!k_strcmp(f, "zegar")) cmd_zegar();
        else if (!k_strcmp(f, "www") || !k_strcmp(f, "przegladarka") || !k_strcmp(f, "internet")) { while (*p == ' ') p++; przegladarka(p); }
        else if (!k_strcmp(f, "siec")) cmd_siec(p);
        else if (!k_strcmp(f, "ping")) cmd_ping(k_token(&p));
        else if (!k_strcmp(f, "spowiedz")) cmd_spowiedz();
        else if (!k_strcmp(f, "komunia")) cmd_komunia();
        else if (!k_strcmp(f, "muzyka")) cmd_muzyka(k_token(&p));
        else if (!k_strcmp(f, "czas") || !k_strcmp(f, "data")) cmd_strefa(0);
        else if (!k_strcmp(f, "strefa")) cmd_strefa(k_token(&p));
        else if (!k_strcmp(f, "system")) cmd_system();
        else if (!k_strcmp(f, "czysc") || !k_strcmp(f, "cls")) { vga_clear(); baner(); }
        else if (!k_strcmp(f, "restart")) cmd_restart();
        else if (!k_strcmp(f, "wylacz")) cmd_wylacz();
        else if (!k_strcmp(f, "parafia")) {
            while (*p == ' ') p++;
            if (*p) { k_strncpy(parafia, p, sizeof parafia); kprintf("Nazwa parafii: %s\n", parafia); }
            else kprintf("Użycie: parafia <nazwa>   np. parafia Parafia pw. św. Józefa w Krakowie\n");
        }
        else if (!k_strcmp(f, "amen")) kprintf("Amen.\n");
        else {
            vga_color(JASNOCZERWONY, CZARNY);
            kprintf("Nieznane polecenie: %s", cmd);
            vga_color(SZARY, CZARNY);
            kprintf(" - wpisz pomoc\n");
        }
    }
}
