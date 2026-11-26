/* AmenOS - kalendarz liturgiczny Kościoła rzymskokatolickiego w Polsce (uproszczony) */
#include "kernel.h"

const char *dni_tyg[7] = { "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota" };
const char *dni_tyg_wielk[7] = { "Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota" };
const char *mies_dop[13] = { "", "stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca", "lipca",
                             "sierpnia", "września", "października", "listopada", "grudnia" };
const char *mies_mian[13] = { "", "Styczeń", "Luty", "Marzec", "Kwiecień", "Maj", "Czerwiec", "Lipiec",
                              "Sierpień", "Wrzesień", "Październik", "Listopad", "Grudzień" };
const char *mies_mian_wielk[13] = { "", "STYCZEŃ", "LUTY", "MARZEC", "KWIECIEŃ", "MAJ", "CZERWIEC", "LIPIEC",
                                    "SIERPIEŃ", "WRZESIEŃ", "PAŹDZIERNIK", "LISTOPAD", "GRUDZIEŃ" };

/* ---------- arytmetyka dat (algorytm H. Hinnanta) ---------- */

int dni_od_epoki(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void data_z_dni(int z, int *r, int *m, int *d) {
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *r = y + (*m <= 2);
}

int dzien_tygodnia(int z) { int w = (z + 4) % 7; return w < 0 ? w + 7 : w; }

int dni_w_miesiacu(int r, int m) {
    static const int t[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((r % 4 == 0 && r % 100 != 0) || r % 400 == 0)) return 29;
    return t[m];
}

/* Wielkanoc - algorytm Meeusa/Jonesa/Butchera (kalendarz gregoriański) */
int wielkanoc(int y) {
    int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4;
    int f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    int mies = (h + l - 7 * m + 114) / 31;
    int dz = (h + l - 7 * m + 114) % 31 + 1;
    return dni_od_epoki(y, mies, dz);
}

/* I Niedziela Adwentu = czwarta niedziela przed Bożym Narodzeniem */
int adwent1(int rok) {
    int bn = dni_od_epoki(rok, 12, 25);
    int w = dzien_tygodnia(bn);
    return bn - (w ? w : 7) - 21;
}

static void rzymska(int n, char *o) {
    static const int v[] = { 10, 9, 5, 4, 1 };
    static const char *s[] = { "X", "IX", "V", "IV", "I" };
    int k = 0;
    for (int i = 0; i < 5; i++)
        while (n >= v[i]) { for (const char *p = s[i]; *p; p++) o[k++] = *p; n -= v[i]; }
    o[k] = 0;
}

/* ---------- kolory szat ---------- */

const char *kolor_nazwa(int k) {
    static const char *n[] = { "zielony", "fioletowy", "biały", "czerwony", "różowy" };
    return n[k];
}
u8 kolor_vga(int k) {
    static const u8 c[] = { ZIELONY, FIOLET, BIALY, CZERWONY, ROZOWY };
    return c[k];
}
u8 kolor_tekst(int k) { return (k == KOL_BIALY || k == KOL_ROZOWY) ? CZARNY : BIALY; }

/* ---------- obchody o stałej dacie ---------- */

enum { R_PRZYWILEJ = 1, R_UROCZ = 2, R_NIEDZ = 3, R_SWIETO = 4, R_UPRZYW = 5, R_WSPOM = 6, R_POWSZ = 7 };

typedef struct { u8 m, d, ranga, kolor, panskie; const char *nazwa; } obchod_t;

#define U(m, d, k, n)  { m, d, R_UROCZ,  k, 0, "Uroczystość " n }
#define UP(m, d, k, n) { m, d, R_UROCZ,  k, 1, "Uroczystość " n }
#define S(m, d, k, n)  { m, d, R_SWIETO, k, 0, "Święto " n }
#define SP(m, d, k, n) { m, d, R_SWIETO, k, 1, "Święto " n }
#define W(m, d, k, n)  { m, d, R_WSPOM,  k, 0, "Wspomnienie " n }
#define B KOL_BIALY
#define C KOL_CZERWONY

static const obchod_t obchody[] = {
    U(1, 1, B, "Świętej Bożej Rodzicielki Maryi"),
    W(1, 2, B, "śś. Bazylego Wielkiego i Grzegorza z Nazjanzu"),
    UP(1, 6, B, "Objawienia Pańskiego (Trzech Króli)"),
    W(1, 17, B, "św. Antoniego, opata"),
    W(1, 21, C, "św. Agnieszki, dziewicy i męczennicy"),
    W(1, 24, B, "św. Franciszka Salezego"),
    S(1, 25, B, "Nawrócenia św. Pawła Apostoła"),
    W(1, 26, B, "śś. Tymoteusza i Tytusa, biskupów"),
    W(1, 28, B, "św. Tomasza z Akwinu"),
    W(1, 31, B, "św. Jana Bosko"),
    SP(2, 2, B, "Ofiarowania Pańskiego (MB Gromnicznej)"),
    W(2, 5, C, "św. Agaty, dziewicy i męczennicy"),
    W(2, 10, B, "św. Scholastyki, dziewicy"),
    S(2, 14, B, "śś. Cyryla i Metodego, patronów Europy"),
    S(2, 22, B, "Katedry św. Piotra Apostoła"),
    U(3, 19, B, "św. Józefa, Oblubieńca NMP"),
    UP(3, 25, B, "Zwiastowania Pańskiego"),
    U(4, 23, C, "św. Wojciecha, głównego patrona Polski"),
    S(4, 25, C, "św. Marka, Ewangelisty"),
    S(4, 29, B, "św. Katarzyny Sieneńskiej, patronki Europy"),
    U(5, 3, B, "NMP Królowej Polski"),
    S(5, 6, C, "śś. Apostołów Filipa i Jakuba"),
    U(5, 8, C, "św. Stanisława, głównego patrona Polski"),
    S(5, 14, C, "św. Macieja Apostoła"),
    S(5, 16, C, "św. Andrzeja Boboli, patrona Polski"),
    S(5, 31, B, "Nawiedzenia NMP"),
    W(6, 13, B, "św. Antoniego z Padwy"),
    U(6, 24, B, "Narodzenia św. Jana Chrzciciela"),
    U(6, 29, C, "śś. Apostołów Piotra i Pawła"),
    S(7, 3, C, "św. Tomasza Apostoła"),
    S(7, 11, B, "św. Benedykta, opata, patrona Europy"),
    S(7, 22, B, "św. Marii Magdaleny"),
    S(7, 23, B, "św. Brygidy, patronki Europy"),
    S(7, 25, C, "św. Jakuba Apostoła"),
    W(7, 26, B, "śś. Joachima i Anny, rodziców NMP"),
    W(7, 29, B, "śś. Marty, Marii i Łazarza"),
    W(7, 31, B, "św. Ignacego z Loyoli"),
    SP(8, 6, B, "Przemienienia Pańskiego"),
    W(8, 8, B, "św. Dominika, prezbitera"),
    S(8, 9, C, "św. Teresy Benedykty od Krzyża (Edyty Stein)"),
    S(8, 10, C, "św. Wawrzyńca, diakona i męczennika"),
    W(8, 14, C, "św. Maksymiliana Marii Kolbego"),
    U(8, 15, B, "Wniebowzięcia NMP"),
    W(8, 22, B, "NMP Królowej"),
    S(8, 24, C, "św. Bartłomieja Apostoła"),
    U(8, 26, B, "NMP Częstochowskiej"),
    W(8, 27, B, "św. Moniki"),
    W(8, 28, B, "św. Augustyna, biskupa i doktora Kościoła"),
    W(8, 29, C, "męczeństwa św. Jana Chrzciciela"),
    S(9, 8, B, "Narodzenia NMP"),
    SP(9, 14, C, "Podwyższenia Krzyża Świętego"),
    W(9, 15, B, "NMP Bolesnej"),
    S(9, 18, B, "św. Stanisława Kostki, patrona Polski"),
    S(9, 21, C, "św. Mateusza, Apostoła i Ewangelisty"),
    S(9, 29, B, "śś. Archaniołów Michała, Gabriela i Rafała"),
    W(9, 30, B, "św. Hieronima"),
    W(10, 1, B, "św. Teresy od Dzieciątka Jezus"),
    W(10, 2, B, "Świętych Aniołów Stróżów"),
    W(10, 4, B, "św. Franciszka z Asyżu"),
    W(10, 7, B, "NMP Różańcowej"),
    W(10, 15, B, "św. Teresy od Jezusa"),
    S(10, 18, C, "św. Łukasza Ewangelisty"),
    W(10, 22, B, "św. Jana Pawła II, papieża"),
    S(10, 28, C, "śś. Apostołów Szymona i Judy Tadeusza"),
    U(11, 1, B, "Wszystkich Świętych"),
    { 11, 2, R_UROCZ, KOL_FIOLETOWY, 0, "Wspomnienie wszystkich wiernych zmarłych" },
    W(11, 4, B, "św. Karola Boromeusza"),
    SP(11, 9, B, "rocznicy poświęcenia Bazyliki Laterańskiej"),
    W(11, 10, B, "św. Leona Wielkiego"),
    W(11, 11, B, "św. Marcina z Tours"),
    W(11, 12, C, "św. Jozafata, biskupa i męczennika"),
    W(11, 13, C, "Pierwszych Męczenników Polski"),
    W(11, 17, B, "św. Elżbiety Węgierskiej"),
    W(11, 21, B, "Ofiarowania NMP"),
    W(11, 22, C, "św. Cecylii, dziewicy i męczennicy"),
    W(11, 24, C, "śś. Andrzeja Dung-Lac i Towarzyszy"),
    S(11, 30, C, "św. Andrzeja Apostoła"),
    W(12, 3, B, "św. Franciszka Ksawerego"),
    U(12, 8, B, "Niepokalanego Poczęcia NMP"),
    W(12, 13, C, "św. Łucji, dziewicy i męczennicy"),
    W(12, 14, B, "św. Jana od Krzyża"),
    S(12, 26, C, "św. Szczepana, pierwszego męczennika"),
    S(12, 27, B, "św. Jana, Apostoła i Ewangelisty"),
    S(12, 28, C, "Świętych Młodzianków, męczenników"),
};

static const obchod_t *szukaj_obchodu(int m, int d) {
    for (u32 i = 0; i < sizeof obchody / sizeof obchody[0]; i++)
        if (obchody[i].m == m && obchody[i].d == d) return &obchody[i];
    return 0;
}

/* nadpisuje obchód dnia, zachowując poprzednią nazwę jako uwagę */
static void ustaw(dzien_lit_t *o, const char *nazwa, int kolor, int ranga) {
    if (o->ranga >= R_UPRZYW) k_strncpy(o->uwaga, o->nazwa, sizeof o->uwaga);
    else ksprintf(o->uwaga, sizeof o->uwaga, "zamiast: %s", o->nazwa);
    k_strncpy(o->nazwa, nazwa, sizeof o->nazwa);
    o->kolor = kolor;
    o->ranga = ranga;
}

/* ---------- okres liturgiczny i obchody ruchome ---------- */

static void sezon(int r, int m, int d, dzien_lit_t *o) {
    memset(o, 0, sizeof *o);
    int D = dni_od_epoki(r, m, d), dow = dzien_tygodnia(D);
    int s = D - dow;                                  /* niedziela otwierająca tydzień */
    int E = wielkanoc(r);
    int popiel = E - 46, palm = E - 7, wczw = E - 3, piecdz = E + 49;
    int adw = adwent1(r), krol = adw - 7;
    int bn = dni_od_epoki(r, 12, 25);
    int jan6 = dni_od_epoki(r, 1, 6);
    int chrzest = jan6 + (7 - dzien_tygodnia(jan6));
    char rz[12];
    const char *dt = dni_tyg_wielk[dow];
    const u32 N = sizeof o->nazwa;

    int rok_lit = (D >= adw) ? r + 1 : r;
    o->cykl_niedz = "CAB"[rok_lit % 3];
    o->cykl_powsz = (rok_lit % 2) ? 1 : 2;
    o->kolor = KOL_ZIELONY;
    o->ranga = R_POWSZ;

    if (D >= adw && D < bn) {
        o->okres = "Adwent";
        o->kolor = KOL_FIOLETOWY;
        int n = (s - adw) / 7 + 1;
        rzymska(n, rz);
        if (dow == 0) {
            ksprintf(o->nazwa, N, "%s Niedziela Adwentu%s", rz, n == 3 ? " (Gaudete)" : "");
            o->ranga = R_PRZYWILEJ;
            if (n == 3) o->kolor = KOL_ROZOWY;
        } else if (m == 12 && d == 24) {
            k_strncpy(o->nazwa, "Wigilia Bożego Narodzenia", N);
            o->ranga = R_UPRZYW;
        } else {
            ksprintf(o->nazwa, N, "%s %s tygodnia Adwentu", dt, rz);
            o->ranga = (m == 12 && d >= 17) ? R_UPRZYW : R_POWSZ;
        }
    } else if (D >= bn || D <= chrzest) {
        o->okres = "Okres Bożego Narodzenia";
        o->kolor = KOL_BIALY;
        int rr = (m == 12) ? r : r - 1;
        int bnr = dni_od_epoki(rr, 12, 25), wbn = dzien_tygodnia(bnr);
        int rodzina = (wbn == 0) ? bnr + 5 : bnr + (7 - wbn);
        if (D == bn) {
            k_strncpy(o->nazwa, "Uroczystość Narodzenia Pańskiego", N);
            o->ranga = R_PRZYWILEJ;
        } else if (D == rodzina) {
            k_strncpy(o->nazwa, "Święto Świętej Rodziny Jezusa, Maryi i Józefa", N);
            o->ranga = R_NIEDZ;
        } else if (m == 12) {
            ksprintf(o->nazwa, N, "%s w oktawie Narodzenia Pańskiego", dt);
            o->ranga = R_UPRZYW;
        } else if (D == chrzest) {
            k_strncpy(o->nazwa, "Święto Chrztu Pańskiego", N);
            o->ranga = R_NIEDZ;
        } else if (dow == 0) {
            k_strncpy(o->nazwa, "II Niedziela po Narodzeniu Pańskim", N);
            o->ranga = R_NIEDZ;
        } else {
            ksprintf(o->nazwa, N, d < 6 ? "%s okresu Bożego Narodzenia" : "%s po Objawieniu Pańskim", dt);
        }
    } else if (D < popiel) {
        o->okres = "Okres zwykły";
        int n = (s == chrzest) ? 1 : (s - chrzest) / 7 + 1;
        rzymska(n, rz);
        if (dow == 0) { ksprintf(o->nazwa, N, "%s Niedziela zwykła", rz); o->ranga = R_NIEDZ; }
        else ksprintf(o->nazwa, N, "%s %s tygodnia zwykłego", dt, rz);
    } else if (D < palm) {
        o->okres = "Wielki Post";
        o->kolor = KOL_FIOLETOWY;
        o->ranga = R_UPRZYW;
        if (D == popiel) { k_strncpy(o->nazwa, "Środa Popielcowa", N); o->ranga = R_PRZYWILEJ; }
        else if (D < popiel + 4) ksprintf(o->nazwa, N, "%s po Popielcu", dt);
        else {
            int n = (s - (popiel + 4)) / 7 + 1;
            rzymska(n, rz);
            if (dow == 0) {
                ksprintf(o->nazwa, N, "%s Niedziela Wielkiego Postu%s", rz, n == 4 ? " (Laetare)" : "");
                o->ranga = R_PRZYWILEJ;
                if (n == 4) o->kolor = KOL_ROZOWY;
            } else ksprintf(o->nazwa, N, "%s %s tygodnia Wielkiego Postu", dt, rz);
        }
    } else if (D < wczw) {
        o->okres = "Wielki Tydzień";
        o->ranga = R_PRZYWILEJ;
        if (D == palm) { k_strncpy(o->nazwa, "Niedziela Palmowa Męki Pańskiej", N); o->kolor = KOL_CZERWONY; }
        else {
            static const char *wt[] = { "", "Wielki Poniedziałek", "Wielki Wtorek", "Wielka Środa" };
            k_strncpy(o->nazwa, wt[dow], N);
            o->kolor = KOL_FIOLETOWY;
        }
    } else if (D < E) {
        o->okres = "Triduum Paschalne";
        o->ranga = R_PRZYWILEJ;
        if (D == wczw) { k_strncpy(o->nazwa, "Wielki Czwartek - Msza Wieczerzy Pańskiej", N); o->kolor = KOL_BIALY; }
        else if (D == E - 2) { k_strncpy(o->nazwa, "Wielki Piątek Męki Pańskiej", N); o->kolor = KOL_CZERWONY; }
        else {
            k_strncpy(o->nazwa, "Wielka Sobota", N);
            k_strncpy(o->uwaga, "wieczorem Wigilia Paschalna - kolor biały", sizeof o->uwaga);
            o->kolor = KOL_FIOLETOWY;
        }
    } else if (D <= piecdz) {
        o->okres = "Okres wielkanocny";
        o->kolor = KOL_BIALY;
        int n = (s - E) / 7 + 1;
        rzymska(n, rz);
        if (D == E) { k_strncpy(o->nazwa, "Niedziela Zmartwychwstania Pańskiego", N); o->ranga = R_PRZYWILEJ; }
        else if (D < E + 7) {
            ksprintf(o->nazwa, N, "%s w oktawie Wielkanocy", dt);
            o->ranga = R_PRZYWILEJ;
        } else if (D == E + 7) {
            k_strncpy(o->nazwa, "II Niedziela Wielkanocna (Miłosierdzia Bożego)", N);
            o->ranga = R_PRZYWILEJ;
        } else if (D == E + 42) {
            k_strncpy(o->nazwa, "Uroczystość Wniebowstąpienia Pańskiego", N);
            o->ranga = R_PRZYWILEJ;
        } else if (D == piecdz) {
            k_strncpy(o->nazwa, "Uroczystość Zesłania Ducha Świętego", N);
            o->kolor = KOL_CZERWONY;
            o->ranga = R_PRZYWILEJ;
        } else if (dow == 0) {
            ksprintf(o->nazwa, N, "%s Niedziela Wielkanocna", rz);
            o->ranga = R_PRZYWILEJ;
        } else ksprintf(o->nazwa, N, "%s %s tygodnia wielkanocnego", dt, rz);
    } else {
        o->okres = "Okres zwykły";
        int n = 34 - (krol - s) / 7;
        rzymska(n, rz);
        if (dow == 0) { ksprintf(o->nazwa, N, "%s Niedziela zwykła", rz); o->ranga = R_NIEDZ; }
        else ksprintf(o->nazwa, N, "%s %s tygodnia zwykłego", dt, rz);

        if (D == piecdz + 1) ustaw(o, "Wspomnienie NMP Matki Kościoła", KOL_BIALY, R_WSPOM);
        else if (D == E + 53) ustaw(o, "Święto Jezusa Chrystusa, Najwyższego i Wiecznego Kapłana", KOL_BIALY, R_SWIETO);
        else if (D == piecdz + 7) ustaw(o, "Uroczystość Najświętszej Trójcy", KOL_BIALY, R_UROCZ);
        else if (D == E + 60) ustaw(o, "Uroczystość Najświętszego Ciała i Krwi Chrystusa", KOL_BIALY, R_UROCZ);
        else if (D == E + 68) ustaw(o, "Uroczystość Najświętszego Serca Pana Jezusa", KOL_BIALY, R_UROCZ);
        else if (D == E + 69) ustaw(o, "Wspomnienie Niepokalanego Serca NMP", KOL_BIALY, R_WSPOM);
        else if (D == krol) ustaw(o, "Uroczystość Jezusa Chrystusa, Króla Wszechświata", KOL_BIALY, R_UROCZ);
    }

}

/* Dokąd przenosi się uroczystość, która wypadła w dzień uprzywilejowany */
static int termin_przeniesienia(int r, const obchod_t *f) {
    int Ds = dni_od_epoki(r, f->m, f->d), E = wielkanoc(r);
    if (f->m == 3 && f->d == 19 && Ds >= E - 7 && Ds < E) return E - 8;  /* św. Józefa: sobota przed Palmową */
    if (f->m == 6 && f->d == 24) return Ds - 1;                         /* św. Jan vs Serce Jezusa: dzień wcześniej */
    int t = Ds;
    for (int i = 0; i < 20; i++) {
        int rr, mm, dd; dzien_lit_t b;
        data_z_dni(t, &rr, &mm, &dd);
        sezon(rr, mm, dd, &b);
        if (b.ranga > R_UROCZ && !(t != Ds && szukaj_obchodu(mm, dd) && szukaj_obchodu(mm, dd)->ranga == R_UROCZ)) break;
        t++;
    }
    return t;
}

/* ---------- główna funkcja: co danego dnia obchodzi Kościół ---------- */

void liturgia(int r, int m, int d, dzien_lit_t *o) {
    sezon(r, m, d, o);
    int D = dni_od_epoki(r, m, d);

    /* obchody o stałej dacie i ich pierwszeństwo */
    const obchod_t *f = szukaj_obchodu(m, d);
    if (f) {
        int wygrywa = f->ranga < o->ranga || (f->panskie && o->ranga == R_NIEDZ);
        if (wygrywa) {
            ustaw(o, f->nazwa, f->kolor, f->ranga);
        } else if (f->ranga == R_UROCZ && o->ranga <= R_UROCZ) {
            int t = termin_przeniesienia(r, f), tr, tm, td;
            data_z_dni(t, &tr, &tm, &td);
            ksprintf(o->uwaga, sizeof o->uwaga, "%s - przeniesiona na %d %s", f->nazwa, td, mies_dop[tm]);
        } else if (f->ranga == R_WSPOM && o->ranga == R_UPRZYW) {
            ksprintf(o->uwaga, sizeof o->uwaga, "lub dowolnie: %s", f->nazwa);
        }
    }

    /* czy dziś przypada uroczystość przeniesiona z wcześniejszego dnia? */
    if (o->ranga <= R_UROCZ) return;
    for (u32 i = 0; i < sizeof obchody / sizeof obchody[0]; i++) {
        const obchod_t *u = &obchody[i];
        if (u->ranga != R_UROCZ) continue;
        int Ds = dni_od_epoki(r, u->m, u->d);
        if (Ds == D || Ds < D - 20 || Ds > D + 1) continue;
        dzien_lit_t b; sezon(r, u->m, u->d, &b);
        if (b.ranga > R_UROCZ) continue;                 /* nie była zablokowana */
        if (termin_przeniesienia(r, u) == D) {
            ustaw(o, u->nazwa, u->kolor, R_UROCZ);
            char tmp[96];
            ksprintf(tmp, sizeof tmp, "przeniesiona z %d %s", u->d, mies_dop[u->m]);
            k_strncpy(o->uwaga, tmp, sizeof o->uwaga);
            return;
        }
    }
}
