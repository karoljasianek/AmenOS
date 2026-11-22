/* AmenOS - Kalkulator biblijny.
 * Zwykły kalkulator (stały przecinek: 6 cyfr po przecinku, liczby 64-bitowe), który zna
 * znaczenie liczb w Piśmie Świętym i ma kilka przycisków z parafialnym przymrużeniem oka. */
#include "kernel.h"

typedef long long s64;
#define SKALA 1000000LL
#define MAKS  9000000000000000000LL           /* ok. 9 bilionów po przeskalowaniu */

/* udostępnione przez gui.c */
void gui_kalk_brudne(int x, int y, int w, int h);

static int CW, CH;
static char wpis[32];                          /* liczba wpisywana z klawiatury */
static s64 wartosc, akum;
static int operacja, nowa, blad, rzymskie;
static char panel[220], panel_tytul[60];
static int wcisniety = -1;

/* ---------- liczby w Piśmie Świętym ---------- */

static const struct { int n; const char *tytul, *opis; } BIBLIA[] = {
    { 1,    "Jeden Bóg",               "Jeden Pan, jedna wiara, jeden chrzest (Ef 4,5)." },
    { 2,    "Dwie ryby",               "Chłopiec miał pięć chlebów i dwie ryby, które Jezus rozmnożył (Mt 14,17)." },
    { 3,    "Trójca Święta",           "W imię Ojca i Syna, i Ducha Świętego (Mt 28,19). Trzeciego dnia zmartwychwstał." },
    { 4,    "Cztery Ewangelie",        "Mateusz, Marek, Łukasz i Jan - cztery opisy Dobrej Nowiny." },
    { 5,    "Pięć chlebów",            "Z pięciu chlebów i dwóch ryb Jezus nakarmił tłum (Mt 14,13-21)." },
    { 6,    "Sześć dni stworzenia",    "W sześć dni Bóg stworzył świat, a siódmego odpoczął (Rdz 1-2)." },
    { 7,    "Siedem darów",            "Siedem darów Ducha Świętego (Iz 11,2) i siedem sakramentów Kościoła." },
    { 8,    "Osiem błogosławieństw",   "Błogosławieni ubodzy w duchu... - Kazanie na Górze (Mt 5,3-10)." },
    { 9,    "Nowenna",                 "Dziewięć dni modlitwy - jak Apostołowie z Maryją przed Pięćdziesiątnicą (Dz 1,14)." },
    { 10,   "Dziesięć przykazań",      "Bóg dał Mojżeszowi Dekalog na górze Synaj (Wj 20,1-17)." },
    { 12,   "Dwunastu Apostołów",      "Jezus powołał Dwunastu, by byli z Nim (Mt 10,1-4)." },
    { 14,   "Droga Krzyżowa",          "Czternaście stacji Drogi Krzyżowej - modlitwa Wielkiego Postu." },
    { 20,   "Tajemnice różańca",       "Dwadzieścia tajemnic: radosne, światła, bolesne i chwalebne." },
    { 27,   "Nowy Testament",          "Nowy Testament liczy 27 ksiąg." },
    { 30,   "Trzydzieści srebrników",  "Za tyle Judasz wydał Jezusa (Mt 26,15)." },
    { 33,   "Lata Jezusa",             "Według tradycji Jezus żył na ziemi około 33 lat." },
    { 40,   "Czterdzieści dni",        "Jezus pościł na pustyni czterdzieści dni (Mt 4,2) - stąd Wielki Post." },
    { 46,   "Stary Testament",         "Stary Testament w kanonie katolickim liczy 46 ksiąg." },
    { 50,   "Pięćdziesiątnica",        "Pięćdziesiąt dni po Wielkanocy - Zesłanie Ducha Świętego (Dz 2,1-4)." },
    { 72,   "Siedemdziesięciu dwóch",  "Jezus wyznaczył siedemdziesięciu dwóch uczniów i posłał ich po dwóch (Łk 10,1)." },
    { 73,   "Pismo Święte",            "Biblia w kanonie katolickim liczy 73 księgi: 46 + 27." },
    { 77,   "Przebaczenie",            "Przebaczać „aż siedemdziesiąt siedem razy” - czyli zawsze (Mt 18,22)." },
    { 99,   "Dziewięćdziesiąt dziewięć", "Pasterz zostawia 99 owiec i szuka jednej zagubionej (Łk 15,4)." },
    { 100,  "Sto owiec",               "Przypowieść o zagubionej owcy: było ich sto (Łk 15,4-7)." },
    { 150,  "Psałterz",                "Księga Psalmów liczy 150 psalmów." },
    { 153,  "Sto pięćdziesiąt trzy",   "Tyle wielkich ryb wyciągnęli uczniowie na słowo Zmartwychwstałego (J 21,11)." },
    { 1000, "Tysiąc lat",              "U Pana jeden dzień jest jak tysiąc lat (2 P 3,8)." },
    { 4000, "Cztery tysiące",          "Siedem chlebów i kilka rybek nakarmiło cztery tysiące mężczyzn (Mt 15,38)." },
    { 5000, "Pięć tysięcy",            "Tyle mężczyzn - nie licząc kobiet i dzieci - nakarmił Jezus (Mt 14,21)." },
};
#define BIBLIA_N ((int)(sizeof BIBLIA / sizeof BIBLIA[0]))

static void ustaw_panel(const char *t, const char *o) {
    k_strncpy(panel_tytul, t, sizeof panel_tytul);
    k_strncpy(panel, o, sizeof panel);
}

static void sprawdz_biblie(s64 v) {
    if (v % SKALA) return;
    s64 n = v / SKALA;
    for (int i = 0; i < BIBLIA_N; i++)
        if (BIBLIA[i].n == n) { ustaw_panel(BIBLIA[i].tytul, BIBLIA[i].opis); return; }
}

/* ---------- arytmetyka stałoprzecinkowa ---------- */

static s64 abs64(s64 x) { return x < 0 ? -x : x; }

static int mnoz(s64 a, s64 b, s64 *w) {
    int znak = (a < 0) != (b < 0);
    a = abs64(a); b = abs64(b);
    s64 ai = a / SKALA, af = a % SKALA;
    if (ai && b > MAKS / ai) return 0;
    s64 r = ai * b + af * (b / SKALA) + af * (b % SKALA) / SKALA;
    if (r > MAKS || r < 0) return 0;
    *w = znak ? -r : r;
    return 1;
}

static int dziel(s64 a, s64 b, s64 *w) {
    if (!b) return 0;
    int znak = (a < 0) != (b < 0);
    a = abs64(a); b = abs64(b);
    while (b > 900000000000000000LL) { b /= 10; a /= 10; }
    s64 q = a / b, r = a % b;
    if (q > MAKS / SKALA) return 0;
    s64 wyn = q * SKALA, mn = SKALA / 10;
    for (int i = 0; i < 6; i++) { r *= 10; wyn += (r / b) * mn; r %= b; mn /= 10; }
    if (r * 2 >= b) wyn++;                      /* zaokrąglenie ostatniej cyfry */
    *w = znak ? -wyn : wyn;
    return 1;
}

static s64 pierw_cal(s64 v) {                  /* pierwiastek całkowity (metoda Newtona) */
    if (v < 2) return v;
    s64 x = v, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}

static void rzymska(s64 v, char *o) {
    static const int w[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *z[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    if (v % SKALA || v <= 0 || v / SKALA > 3999) { k_strncpy(o, "(tylko liczby całkowite 1-3999)", 40); return; }
    int n = (int)(v / SKALA), k = 0;
    for (int i = 0; i < 13; i++) while (n >= w[i]) { for (const char *p = z[i]; *p; p++) o[k++] = *p; n -= w[i]; }
    o[k] = 0;
}

static void formatuj(s64 v, char *o, u32 max) {
    char t[48]; int k = 0;
    int ujemna = v < 0;
    v = abs64(v);
    s64 c = v / SKALA, f = v % SKALA;
    char cyfry[24]; int n = 0;
    do { cyfry[n++] = (char)('0' + c % 10); c /= 10; } while (c);
    if (ujemna) t[k++] = '-';
    for (int i = n - 1; i >= 0; i--) { t[k++] = cyfry[i]; if (i && i % 3 == 0) t[k++] = ' '; }
    if (f) {
        t[k++] = ',';
        char ul[7];
        for (int i = 5; i >= 0; i--) { ul[i] = (char)('0' + f % 10); f /= 10; }
        int dl = 6;
        while (dl > 0 && ul[dl - 1] == '0') dl--;
        for (int i = 0; i < dl; i++) t[k++] = ul[i];
    }
    t[k] = 0;
    k_strncpy(o, t, max);
}

static s64 wpis_wartosc(void) {
    s64 c = 0, f = 0, mn = SKALA / 10;
    int po = 0, ujemna = 0;
    for (const char *p = wpis; *p; p++) {
        if (*p == '-') ujemna = 1;
        else if (*p == ',') po = 1;
        else if (!po) c = c * 10 + (*p - '0');
        else if (mn) { f += (*p - '0') * mn; mn /= 10; }
    }
    s64 v = c * SKALA + f;
    return ujemna ? -v : v;
}

static void pokaz_blad(const char *t, const char *o) {
    blad = 1; wpis[0] = 0; operacja = 0; nowa = 1;
    ustaw_panel(t, o);
}

static void licz(void) {
    s64 b = wpis[0] ? wpis_wartosc() : wartosc, w = 0;
    int ok = 1;
    switch (operacja) {
    case '+': w = akum + b; ok = abs64(w) <= MAKS; break;
    case '-': w = akum - b; ok = abs64(w) <= MAKS; break;
    case '*': ok = mnoz(akum, b, &w); break;
    case '/':
        if (!b) { pokaz_blad("Dzielenie przez zero", "Nieskończony jest tylko Bóg. Spróbuj podzielić przez coś innego."); return; }
        ok = dziel(akum, b, &w);
        break;
    default: w = b;
    }
    if (!ok) { pokaz_blad("Za duża liczba", "Policz gwiazdy, jeśli zdołasz (Rdz 15,5) - ten kalkulator liczy do 9 bilionów."); return; }
    wartosc = w;
}

/* ---------- przyciski ---------- */

enum { K_CYFRA, K_PRZECINEK, K_OP, K_ROWNA, K_PRZEBACZ, K_COFNIJ, K_PIERW, K_ZNAK, K_DZIESIECINA, K_ROZMNOZ,
       K_WIELKANOC, K_BN, K_RZYM };
typedef struct { const char *t; int typ, arg, kol, wiersz, szer; } przycisk_t;
static const przycisk_t PRZ[] = {
    { "Przebacz", K_PRZEBACZ, 0, 0, 0, 1 }, { "←", K_COFNIJ, 0, 1, 0, 1 }, { "√", K_PIERW, 0, 2, 0, 1 }, { "÷", K_OP, '/', 3, 0, 1 },
    { "7", K_CYFRA, 7, 0, 1, 1 }, { "8", K_CYFRA, 8, 1, 1, 1 }, { "9", K_CYFRA, 9, 2, 1, 1 }, { "×", K_OP, '*', 3, 1, 1 },
    { "4", K_CYFRA, 4, 0, 2, 1 }, { "5", K_CYFRA, 5, 1, 2, 1 }, { "6", K_CYFRA, 6, 2, 2, 1 }, { "−", K_OP, '-', 3, 2, 1 },
    { "1", K_CYFRA, 1, 0, 3, 1 }, { "2", K_CYFRA, 2, 1, 3, 1 }, { "3", K_CYFRA, 3, 2, 3, 1 }, { "+", K_OP, '+', 3, 3, 1 },
    { "±", K_ZNAK, 0, 0, 4, 1 }, { "0", K_CYFRA, 0, 1, 4, 1 }, { ",", K_PRZECINEK, 0, 2, 4, 1 }, { "=", K_ROWNA, 0, 3, 4, 1 },
    { "Dziesięcina", K_DZIESIECINA, 0, 0, 5, 2 }, { "Rozmnóż ×1000", K_ROZMNOZ, 0, 2, 5, 2 },
    { "Do Wielkanocy", K_WIELKANOC, 0, 0, 6, 2 }, { "Do Bożego Nar.", K_BN, 0, 2, 6, 2 },
    { "Liczby rzymskie", K_RZYM, 0, 0, 7, 4 },
};
#define PRZ_N ((int)(sizeof PRZ / sizeof PRZ[0]))
#define BX0 10
#define BY0 182
#define BW 72
#define BH 38
#define BG 6

static void prz_prost(int i, int *x, int *y, int *w, int *h) {
    *x = BX0 + PRZ[i].kol * (BW + BG);
    *y = BY0 + PRZ[i].wiersz * (BH + BG);
    *w = PRZ[i].szer * BW + (PRZ[i].szer - 1) * BG;
    *h = BH;
}

static int dni_do(int cel_m, int cel_d, int wielkanoc_cel, char *data, u32 max) {
    czas_t c; rtc_czytaj(&c);
    int dzis = dni_od_epoki(c.rok, c.mies, c.dzien);
    int r = c.rok, cel = wielkanoc_cel ? wielkanoc(r) : dni_od_epoki(r, cel_m, cel_d);
    if (cel < dzis) { r++; cel = wielkanoc_cel ? wielkanoc(r) : dni_od_epoki(r, cel_m, cel_d); }
    int rr, mm, dd; data_z_dni(cel, &rr, &mm, &dd);
    ksprintf(data, max, "%d %s %d (%s)", dd, mies_dop[mm], rr, dni_tyg[dzien_tygodnia(cel)]);
    return cel - dzis;
}

static void akcja(int i) {
    const przycisk_t *p = &PRZ[i];
    char t[200], d[64];
    if (blad && p->typ != K_PRZEBACZ && p->typ != K_CYFRA && p->typ != K_RZYM && p->typ != K_WIELKANOC && p->typ != K_BN) return;
    if (blad) { blad = 0; wartosc = 0; akum = 0; }
    switch (p->typ) {
    case K_CYFRA: {
        if (nowa) { wpis[0] = 0; nowa = 0; }
        int n = (int)k_strlen(wpis), cyfr = 0;
        for (const char *q = wpis; *q; q++) if (*q >= '0' && *q <= '9') cyfr++;
        if (cyfr >= 13) break;
        if (!k_strcmp(wpis, "0")) n = 0;
        if (!k_strcmp(wpis, "-0")) n = 1;
        wpis[n] = (char)('0' + p->arg); wpis[n + 1] = 0;
        break;
    }
    case K_PRZECINEK: {
        if (nowa) { wpis[0] = 0; nowa = 0; }
        int jest = 0; for (const char *q = wpis; *q; q++) if (*q == ',') jest = 1;
        if (jest) break;
        if (!wpis[0] || !k_strcmp(wpis, "-")) { int n = (int)k_strlen(wpis); wpis[n] = '0'; wpis[n + 1] = 0; }
        { int n = (int)k_strlen(wpis); wpis[n] = ','; wpis[n + 1] = 0; }
        break;
    }
    case K_OP:
        if (operacja && wpis[0]) { licz(); if (blad) break; }
        else if (wpis[0]) wartosc = wpis_wartosc();
        akum = wartosc; operacja = p->arg; wpis[0] = 0; nowa = 1;
        break;
    case K_ROWNA:
        if (!operacja) { if (wpis[0]) wartosc = wpis_wartosc(); }
        else licz();
        if (blad) break;
        operacja = 0; wpis[0] = 0; nowa = 1;
        sprawdz_biblie(wartosc);
        break;
    case K_PRZEBACZ:
        wpis[0] = 0; wartosc = 0; akum = 0; operacja = 0; nowa = 1; blad = 0;
        ustaw_panel("Przebaczone!", "Wszystkie błędy zostały odpuszczone. Możesz zacząć liczyć od nowa.");
        break;
    case K_COFNIJ:
        if (wpis[0] && !nowa) { int n = (int)k_strlen(wpis); wpis[n - 1] = 0; if (!k_strcmp(wpis, "-")) wpis[0] = 0; }
        break;
    case K_ZNAK:
        if (wpis[0] && !nowa) {
            if (wpis[0] == '-') memcpy(wpis, wpis + 1, k_strlen(wpis));
            else { u32 n = k_strlen(wpis); if (n + 2 < sizeof wpis) { for (int j = (int)n; j >= 0; j--) wpis[j + 1] = wpis[j]; wpis[0] = '-'; } }
        } else wartosc = -wartosc;
        break;
    case K_PIERW: {
        s64 v = (wpis[0] && !nowa) ? wpis_wartosc() : wartosc;
        if (v < 0) { pokaz_blad("Pierwiastek z liczby ujemnej", "Tajemnica większa niż ten kalkulator. Spróbuj z liczbą dodatnią."); break; }
        wartosc = v < 9000000000000LL ? pierw_cal(v * SKALA) : pierw_cal(v) * 1000;
        wpis[0] = 0; nowa = 1;
        sprawdz_biblie(wartosc);
        break;
    }
    case K_DZIESIECINA: {
        s64 v = (wpis[0] && !nowa) ? wpis_wartosc() : wartosc;
        wartosc = v / 10; wpis[0] = 0; nowa = 1; operacja = 0;
        formatuj(wartosc, d, sizeof d);
        ksprintf(t, sizeof t, "Dziesiąta część to %s. „Przynieście całą dziesięcinę do spichlerza” (Ml 3,10).", d);
        ustaw_panel("Dziesięcina", t);
        break;
    }
    case K_ROZMNOZ: {
        s64 v = (wpis[0] && !nowa) ? wpis_wartosc() : wartosc, w;
        if (!mnoz(v, 1000 * SKALA, &w)) { pokaz_blad("Za duża liczba", "Nawet cud ma swoje granice w tym kalkulatorze."); break; }
        wartosc = w; wpis[0] = 0; nowa = 1; operacja = 0;
        ustaw_panel("Rozmnożenie", "Jezus rozmnożył pięć chlebów i dwie ryby - i wszyscy najedli się do syta (Mt 14,13-21).");
        sprawdz_biblie(wartosc);
        break;
    }
    case K_WIELKANOC: case K_BN: {
        int wiel = p->typ == K_WIELKANOC;
        int n = dni_do(12, 25, wiel, d, sizeof d);
        wartosc = (s64)n * SKALA; wpis[0] = 0; nowa = 1; operacja = 0; blad = 0;
        if (!n) ustaw_panel(wiel ? "Wielkanoc!" : "Boże Narodzenie!", wiel ? "Dziś Zmartwychwstanie Pańskie. Alleluja!" : "Dziś narodził się Zbawiciel. Chwała na wysokości Bogu!");
        else {
            ksprintf(t, sizeof t, "Do %s zostało %d %s: %s.", wiel ? "Wielkanocy" : "Bożego Narodzenia", n, n == 1 ? "dzień" : "dni", d);
            ustaw_panel(wiel ? "Ile dni do Wielkanocy" : "Ile dni do Bożego Narodzenia", t);
        }
        break;
    }
    case K_RZYM: rzymskie = !rzymskie; break;
    }
    gui_kalk_brudne(0, 0, CW, CH);
}

/* ---------- rysowanie ---------- */

void kalk_rysuj(int x, int y) {
    fb_prost(x, y, CW, CH, 0xEDEBE6);
    /* wyświetlacz */
    fb_prost_zaokr(x + 10, y + 10, CW - 20, 76, 6, 0x1C2A4A);
    char t[64], r[48];
    const char *op = operacja == '+' ? "+" : operacja == '-' ? "−" : operacja == '*' ? "×" : operacja == '/' ? "÷" : "";
    if (operacja) { formatuj(akum, t, sizeof t); char o[80]; ksprintf(o, sizeof o, "%s %s", t, op); fb_tekst(x + CW - 20 - fb_tekst_szer(o, 1), y + 16, o, 0x8CA0C8, FB_PRZEZR, 1); }
    if (blad) k_strncpy(t, "Błąd", sizeof t);
    else if (wpis[0] && !nowa) k_strncpy(t, wpis, sizeof t);
    else formatuj(wartosc, t, sizeof t);
    int sk = fb_tekst_szer(t, 2) <= CW - 40 ? 2 : 1;
    fb_tekst(x + CW - 20 - fb_tekst_szer(t, sk), y + 36, t, 0xFFFFFF, FB_PRZEZR, sk);
    if (rzymskie) {
        rzymska((wpis[0] && !nowa) ? wpis_wartosc() : wartosc, r);
        fb_tekst(x + 20, y + 16, r, 0xF2C230, FB_PRZEZR, 1);
    }
    /* panel z Pisma Świętego */
    fb_prost_zaokr(x + 10, y + 94, CW - 20, 80, 6, 0xC9A43A);
    fb_prost_zaokr(x + 12, y + 96, CW - 24, 76, 5, 0xFBF6E6);
    fb_ikona(x + 18, y + 102, fb_ikona_nr(11), 1);
    fb_tekst(x + 38, y + 102, panel_tytul, 0x7A5A10, FB_PRZEZR, 1);
    fb_tekst(x + 39, y + 102, panel_tytul, 0x7A5A10, FB_PRZEZR, 1);
    fb_tekst_zawin(x + 18, y + 122, panel, 35, 0x3A3020, 16);
    /* przyciski */
    for (int i = 0; i < PRZ_N; i++) {
        int bx, by, bw, bh;
        prz_prost(i, &bx, &by, &bw, &bh);
        const przycisk_t *p = &PRZ[i];
        u32 tlo = p->typ == K_CYFRA || p->typ == K_PRZECINEK || p->typ == K_ZNAK ? 0xFFFFFF :
                  p->typ == K_ROWNA ? 0x2C5AA0 : p->typ == K_PRZEBACZ ? 0x6A1B9A :
                  p->typ == K_OP ? 0xDCE4F2 : p->typ == K_RZYM && rzymskie ? 0xF2C230 :
                  p->typ >= K_DZIESIECINA ? 0xF4EBCF : 0xE4E2DC;
        u32 tk = (p->typ == K_ROWNA || p->typ == K_PRZEBACZ) ? 0xFFFFFF : 0x1E2230;
        if (i == wcisniety) tlo = fb_mieszaj(tlo, 0x000000, 50);
        fb_prost_zaokr(x + bx + 1, y + by + 2, bw, bh, 5, 0xC8C4BA);
        fb_prost_zaokr(x + bx, y + by, bw, bh, 5, tlo);
        int s = (p->typ == K_CYFRA || p->typ == K_OP || p->typ == K_ROWNA) ? 2 : 1;
        if (fb_tekst_szer(p->t, s) > bw - 8) s = 1;
        fb_tekst(x + bx + (bw - fb_tekst_szer(p->t, s)) / 2, y + by + (bh - 16 * s) / 2, p->t, tk, FB_PRZEZR, s);
    }
}

void kalk_klik(int lx, int ly) {
    for (int i = 0; i < PRZ_N; i++) {
        int bx, by, bw, bh;
        prz_prost(i, &bx, &by, &bw, &bh);
        if (lx >= bx && lx < bx + bw && ly >= by && ly < by + bh) { wcisniety = i; akcja(i); return; }
    }
}

void kalk_puszczenie(void) { if (wcisniety >= 0) { wcisniety = -1; gui_kalk_brudne(0, BY0 - 4, CW, CH - BY0 + 4); } }

static int znajdz(int typ, int arg) { for (int i = 0; i < PRZ_N; i++) if (PRZ[i].typ == typ && PRZ[i].arg == arg) return i; return -1; }

void kalk_klawisz(int k) {
    int i = -1;
    if (k >= '0' && k <= '9') i = znajdz(K_CYFRA, k - '0');
    else if (k == '+' || k == '-' || k == '*' || k == '/') i = znajdz(K_OP, k);
    else if (k == KEY_ENTER || k == '=') i = znajdz(K_ROWNA, 0);
    else if (k == ',' || k == '.') i = znajdz(K_PRZECINEK, 0);
    else if (k == KEY_BKSP) i = znajdz(K_COFNIJ, 0);
    else if (k == 'c' || k == 'C') i = znajdz(K_PRZEBACZ, 0);
    else if (k == 'r' || k == 'R') i = znajdz(K_RZYM, 0);
    if (i >= 0) akcja(i);
}

void kalk_init(int cw, int ch) {
    CW = cw; CH = ch;
    nowa = 1;
    ustaw_panel("Kalkulator biblijny", "Liczby w Piśmie Świętym mają znaczenie. Policz coś - może trafisz na jedną z nich!");
}

int kalk_szer(void) { return BX0 * 2 + 4 * BW + 3 * BG; }
int kalk_wys(void) { return BY0 + 8 * (BH + BG) + 6; }
