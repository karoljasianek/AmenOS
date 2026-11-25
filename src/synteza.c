/* AmenOS - syntezator dźwięku dla prawdziwej karty dźwiękowej.
 *
 * 48 000 próbek na sekundę, stereo, 16 bitów. Do 40 głosów naraz: organy (rejestry 16' 8' 4'
 * 2 2/3' 2' i delikatna "voix céleste"), flet, dzwony kościelne (alikwoty jak w prawdziwym
 * dzwonie, każdy wybrzmiewa w swoim tempie), dzwonki ministranckie i szum. Na końcu pogłos
 * (filtry grzebieniowe i wszechprzepustowe w stylu Freeverb), żeby brzmiało jak w kościele.
 * Wszystko na liczbach całkowitych - system nie używa koprocesora. */
#include "kernel.h"
#include "audio_dane.h"

#define FS    48000
#define BLOK  16
#define GLOSY 40

typedef struct {
    u8  typ;                        /* 0 wolny, 1 fala z tablicy, 2 szum */
    u8  etap;                       /* 1 narastanie, 2 trwanie lub zanik, 3 wybrzmiewanie */
    u8  zrodlo;                     /* 1 = utwór z sekwencera */
    u16 id;
    const short *tab;
    u32 faza, krok;
    int amp, a_krok;                /* amplituda Q16 (65536 = pełna) */
    int zanik, pusc;                /* mnożniki obwiedni na blok, Q16 */
    int glos, pl, pp, poglos;       /* 0..256 */
    u32 bloki;                      /* po ilu blokach puścić klawisz (0 = trzymaj) */
    u32 los;
    int lp;
    u32 lfo, lfo_krok;
} glos_t;

static glos_t gl[GLOSY];
static u16 nast_id = 1;
static u32 probka;                  /* czas syntezatora: próbki wygenerowane od startu */
static int glosnosc = 200;
static u32 cisza;                   /* jak długo nic nie gra (w próbkach) */
static u32 los_stan = 0x2026AE11;

static u32 los(void) { los_stan = los_stan * 1664525u + 1013904223u; return los_stan >> 8; }

static u32 krok_mhz(u32 mhz) { return (u32)(((unsigned long long)mhz << 32) / (FS * 1000ull)); }
static int mnoznik(u32 tau_ms) {                /* exp(-BLOK/tau) w Q16 */
    u32 d = (65536u * BLOK) / (tau_ms * (FS / 1000) + 1);
    return d >= 65536 ? 0 : (int)(65536 - d);
}

static glos_t *nowy(u16 id) {
    glos_t *g = 0;
    for (int i = 0; i < GLOSY; i++) if (!gl[i].typ) { g = &gl[i]; break; }
    if (!g) {                                   /* brak wolnych: zabierz najcichszy */
        int min = 0x7FFFFFFF;
        for (int i = 0; i < GLOSY; i++) {
            int a = gl[i].amp - (gl[i].etap == 3 ? 40000 : 0);
            if (a < min) { min = a; g = &gl[i]; }
        }
    }
    memset(g, 0, sizeof *g);
    g->id = id;
    return g;
}

static glos_t *fala(u16 id, const short *tab, u32 krok, int glos, u32 atak_ms, u32 tau_ms, u32 pusc_ms, int poglos, int pan) {
    glos_t *g = nowy(id);
    g->typ = 1; g->tab = tab; g->krok = krok;
    g->faza = los() << 12;
    if (atak_ms) { g->etap = 1; g->a_krok = (int)(65536u * BLOK / (atak_ms * (FS / 1000))); }
    else { g->etap = 2; g->amp = 65536; }
    g->zanik = tau_ms ? mnoznik(tau_ms) : 65536;
    g->pusc = mnoznik(pusc_ms);
    g->glos = glos; g->poglos = poglos;
    if (pan < -100) pan = -100;
    if (pan > 100) pan = 100;
    g->pl = 180 - pan; g->pp = 180 + pan;
    return g;
}

u32 dzw_mhz(int nuta) { return NUTA_MHZ[nuta & 127]; }

/* ---------- instrumenty ---------- */

u16 dzw_nuta(int ins, u32 mhz, u32 ms, int glos) {
    u16 id = nast_id++;
    if (!nast_id) nast_id = 1;
    u32 k = krok_mhz(mhz);
    int pan = (int)(mhz / 1000) / 20 - 20;      /* wysokie trochę w prawo, niskie w lewo - jak piszczałki */
    glos_t *g = 0;
    switch (ins) {
    case INS_ORGANY:
        g = fala(id, ORG_T, k / 2, glos, 28, 0, 110, 200, pan);
        if (g) { glos_t *c = fala(id, ORG_T, k / 2 + k / 1300, glos / 3, 60, 0, 140, 220, -pan); if (ms) c->bloki = ms * (FS / 1000) / BLOK; }
        break;
    case INS_FLET:
        g = fala(id, FLET_T, k, glos, 45, 0, 140, 190, pan);
        break;
    case INS_PIK: {                             /* krótki dźwięk powiadomienia */
        g = fala(id, SIN_T, k, glos, 2, 260, 60, 90, 0);
        fala(id, SIN_T, k * 2, glos / 3, 2, 120, 40, 60, 0);
        fala(id, SIN_T, k * 3, glos / 8, 2, 60, 30, 40, 0);
        break;
    }
    case INS_DZWONEK: {                         /* mały dzwonek ręczny: alikwoty nieharmoniczne */
        static const short r[4] = { 1000, 2756, 5404, 8933 }, a[4] = { 256, 120, 60, 30 }, t[4] = { 420, 190, 80, 40 };
        int p = (int)(los() % 120) - 60;
        for (int i = 0; i < 4; i++) {
            glos_t *x = fala(id, SIN_T, krok_mhz(mhz / 1000 * r[i]), glos * a[i] / 256, 1, t[i], 60, 150, p);
            if (!i) g = x;
        }
        break;
    }
    case INS_KLIK:
        g = fala(id, SIN_T, k, glos, 1, 18, 10, 20, 0);
        break;
    }
    if (g && ms) for (int i = 0; i < GLOSY; i++) if (gl[i].id == id && gl[i].typ && !gl[i].bloki) gl[i].bloki = ms * (FS / 1000) / BLOK;
    return id;
}

void dzw_pusc(u16 id) { for (int i = 0; i < GLOSY; i++) if (gl[i].typ && gl[i].id == id) gl[i].etap = 3; }

/* dzwon kościelny: alikwoty (stosunek do tonu podstawowego w tysięcznych) i czas wybrzmiewania */
void dzw_dzwon(u32 mhz, int glos) {
    static const short r[9] = { 500, 1000, 1189, 1498, 2000, 2520, 2990, 4070, 5010 };
    static const short a[9] = { 150, 120, 110, 60, 170, 70, 50, 28, 18 };
    static const short t[9] = { 4500, 3200, 2400, 1600, 1800, 1000, 650, 380, 240 };
    u16 id = nast_id++;
    for (int i = 0; i < 9; i++) fala(id, SIN_T, krok_mhz(mhz / 1000 * r[i]), glos * a[i] / 420, 0, t[i], 400, 230, i * 9 - 36);
    glos_t *s = nowy(id);                       /* uderzenie serca dzwonu */
    s->typ = 2; s->etap = 2; s->amp = 65536; s->zanik = mnoznik(25); s->pusc = mnoznik(10);
    s->glos = glos / 6; s->pl = s->pp = 180; s->poglos = 120; s->los = los();
}

static u16 szum_id;
void dzw_szum(u32 ms, int glos) {
    glos_t *s = nowy(szum_id = nast_id++);
    s->typ = 2; s->etap = 1; s->a_krok = 65536 * BLOK / (40 * 48); s->zanik = 65536; s->pusc = mnoznik(80);
    s->glos = glos; s->pl = s->pp = 180; s->poglos = 40; s->los = los();
    s->lfo_krok = krok_mhz(31000);              /* "wrrr" */
    s->bloki = ms * (FS / 1000) / BLOK;
}

/* ---------- zdarzenia zaplanowane w czasie syntezatora ---------- */

enum { Z_NUTA, Z_DZWON, Z_DZWONEK };
typedef struct { u32 kiedy; u8 co, ins; u16 glos; u32 mhz, ms; } zdarzenie_t;
#define ZD 128
static zdarzenie_t zd[ZD];
static int zdn;

static void zaplanuj(u32 za_ms, int co, int ins, int glos, u32 mhz, u32 ms) {
    if (zdn == ZD) return;
    zdarzenie_t *z = &zd[zdn++];
    z->kiedy = probka + za_ms * (FS / 1000); z->co = (u8)co; z->ins = (u8)ins; z->glos = (u16)glos; z->mhz = mhz; z->ms = ms;
}

void dzw_pozniej(u32 za_ms, int ins, u32 mhz, u32 ms, int glos) { zaplanuj(za_ms, Z_NUTA, ins, glos, mhz, ms); }

/* dzwonki ministranckie: kilka małych dzwonków potrząsanych naraz; zwraca czas w ms */
u32 dzw_dzwonki(int serie) {
    static const u32 f[4] = { 2093000, 2349000, 2637000, 3136000 };
    u32 t = 0;
    for (int s = 0; s < serie; s++) {
        for (u32 k = 0; k < 450; k += 38 + los() % 14) zaplanuj(t + k, Z_DZWONEK, 0, 34 + (int)(los() % 26), f[los() % 4], 0);
        t += 670;
    }
    return t;
}

/* dzwon wybija n razy co odstep ms */
void dzw_bij(int n, u32 mhz, u32 odstep) { for (int i = 0; i < n; i++) zaplanuj((u32)i * odstep, Z_DZWON, 0, 200, mhz, 0); }

/* gotowe efekty */
void dzw_efekt(int e) {
    switch (e) {
    case E_POWIADOM:   dzw_nuta(INS_PIK, 783990, 0, 110); dzw_pozniej(110, INS_PIK, 1046500, 0, 110); break;
    case E_WIADOMOSC:  dzw_nuta(INS_PIK, 1318510, 0, 80); dzw_pozniej(70, INS_PIK, 1567980, 0, 70); break;
    case E_KLIK:       dzw_nuta(INS_KLIK, 1760000, 0, 90); break;
    case E_PACIOREK:   dzw_nuta(INS_KLIK, 1174660, 0, 120); dzw_nuta(INS_PIK, 2349320, 0, 25); break;
    case E_START:                                /* dzwon i akord organów */
        dzw_dzwon(196000, 180);
        dzw_pozniej(500, INS_ORGANY, 130813, 2600, 70); dzw_pozniej(500, INS_ORGANY, 261626, 2600, 80);
        dzw_pozniej(500, INS_ORGANY, 329628, 2600, 70); dzw_pozniej(500, INS_ORGANY, 391995, 2600, 70);
        break;
    case E_AMEN:                                 /* kadencja plagalna "A-men": F-dur, potem C-dur */
        dzw_nuta(INS_ORGANY, 174614, 750, 90); dzw_nuta(INS_ORGANY, 349228, 750, 80);
        dzw_nuta(INS_ORGANY, 440000, 750, 80); dzw_nuta(INS_ORGANY, 523251, 750, 80);
        dzw_pozniej(800, INS_ORGANY, 130813, 1600, 90); dzw_pozniej(800, INS_ORGANY, 329628, 1600, 80);
        dzw_pozniej(800, INS_ORGANY, 391995, 1600, 80); dzw_pozniej(800, INS_ORGANY, 523251, 1600, 80);
        break;
    case E_AKORD:
        for (int i = 0; i < 4; i++) dzw_pozniej((u32)i * 90, INS_FLET, NUTA_MHZ[67 + (int)(los() % 12)], 900, 70);
        break;
    case E_NISZCZARKA: dzw_szum(1700, 120); break;
    }
}

/* ---------- utwór grany próbka w próbkę (bez drgania tempa) ---------- */

static const utwor_t *ut;
static int ut_i, ut_gra;
static u32 ut_nast, ut_start;

void dzw_utwor(const utwor_t *u) {
    dzw_utwor_stop();
    ut = u; ut_i = 0; ut_nast = ut_start = probka + 2 * BLOK; ut_gra = 1;
}
void dzw_utwor_stop(void) {
    ut_gra = 0;
    for (int i = 0; i < GLOSY; i++) if (gl[i].typ && gl[i].zrodlo) gl[i].etap = 3;
}
int dzw_utwor_gra(void) { return ut_gra; }
u32 dzw_utwor_ms(void) { return ut_gra ? (probka - ut_start) / (FS / 1000) : 0; }

static void sekwencer(void) {
    while (ut_gra && (int)(ut_nast - probka) < BLOK) {
        if (ut_i >= ut->n) { ut_gra = 0; break; }
        u8 n = ut->nuty[ut_i];
        u32 dl = ut->czasy[ut_i];
        if (n) {
            u16 id = dzw_nuta(INS_ORGANY, NUTA_MHZ[n], dl * 93 / 100, 150);
            for (int i = 0; i < GLOSY; i++) if (gl[i].id == id) gl[i].zrodlo = 1;
        }
        ut_nast += dl * (FS / 1000);
        ut_i++;
    }
    for (int i = 0; i < zdn; i++)
        if ((int)(zd[i].kiedy - probka) < BLOK) {
            zdarzenie_t z = zd[i];
            zd[i--] = zd[--zdn];
            if (z.co == Z_NUTA) dzw_nuta(z.ins, z.mhz, z.ms, z.glos);
            else if (z.co == Z_DZWON) dzw_dzwon(z.mhz, z.glos);
            else dzw_nuta(INS_DZWONEK, z.mhz, 0, z.glos);
        }
}

/* ---------- pogłos ---------- */

#define NK 4
static const short KD[NK] = { 1693, 1759, 1621, 1549 };
static const short AD[2] = { 605, 481 };
static short kb[2][NK][1800], ab[2][2][640];
static int ki[2][NK], kf[2][NK], ai[2][2];
#define FEED 28500                               /* długość pogłosu (ok. 2,5 s - spory kościół) */
#define DAMP 9000                                /* tłumienie wysokich tonów */

static inline int nasyc(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static inline int miekko(int v) {               /* łagodny ogranicznik: powyżej 22000 ściska 4:1 */
    if (v > 22000) v = 22000 + (v - 22000) / 4;
    else if (v < -22000) v = -22000 + (v + 22000) / 4;
    return nasyc(v);
}

static int poglos(int c, int in) {
    int s = 0;
    for (int k = 0; k < NK; k++) {
        int n = KD[k] + c * 23, i = ki[c][k];
        short *b = kb[c][k];
        int o = b[i];
        kf[c][k] = (o * (32768 - DAMP) + kf[c][k] * DAMP) >> 15;
        b[i] = (short)nasyc(in + ((kf[c][k] * FEED) >> 15));
        ki[c][k] = i + 1 == n ? 0 : i + 1;
        s += o;
    }
    for (int a = 0; a < 2; a++) {
        int n = AD[a] + c * 23, i = ai[c][a];
        short *b = ab[c][a];
        int o = b[i], wy = o - s;
        b[i] = (short)nasyc(s + (o >> 1));
        ai[c][a] = i + 1 == n ? 0 : i + 1;
        s = wy;
    }
    return s;
}

/* ---------- generowanie ---------- */

void synteza_glosnosc(int g) { glosnosc = g < 0 ? 0 : (g > 256 ? 256 : g); }
int  synteza_glosnosc_pobierz(void) { return glosnosc; }
void synteza_cisza(void) { for (int i = 0; i < GLOSY; i++) gl[i].typ = 0; zdn = 0; ut_gra = 0; }

void synteza_generuj(short *out, int ramek) {
    while (ramek > 0) {
        int n = ramek < BLOK ? ramek : BLOK;
        sekwencer();
        int aktywne = 0;
        for (int i = 0; i < GLOSY; i++) if (gl[i].typ) { aktywne = 1; break; }
        if (!aktywne && cisza > FS * 4) {        /* długa cisza: zera, bez liczenia pogłosu */
            memset(out, 0, (u32)n * 4u);
            out += 2 * n; ramek -= n; probka += (u32)n;
            continue;
        }
        if (aktywne) cisza = 0;
        else if ((cisza += (u32)n) > FS * 4) {   /* pogłos wybrzmiał - wyczyść bufory */
            memset(kb, 0, sizeof kb); memset(ab, 0, sizeof ab); memset(kf, 0, sizeof kf);
        }
        int L[BLOK], R[BLOK], W[BLOK];
        for (int j = 0; j < n; j++) L[j] = R[j] = W[j] = 0;
        for (int i = 0; i < GLOSY; i++) {
            glos_t *g = &gl[i];
            if (!g->typ) continue;
            /* obwiednia raz na blok */
            if (g->etap == 1) { g->amp += g->a_krok; if (g->amp >= 65536) { g->amp = 65536; g->etap = 2; } }
            else if (g->etap == 2) g->amp = (int)(((long long)g->amp * g->zanik) >> 16);
            else g->amp = (int)(((long long)g->amp * g->pusc) >> 16);
            if (g->etap != 3 && g->bloki && --g->bloki == 0) g->etap = 3;
            if (g->amp < 40 && g->etap != 1) { g->typ = 0; continue; }
            int a = g->amp >> 1;
            int pl = g->pl * g->glos >> 8, pp = g->pp * g->glos >> 8, pw = g->poglos * g->glos >> 8;
            if (g->typ == 1) {
                const short *t = g->tab;
                u32 f = g->faza, k = g->krok;
                for (int j = 0; j < n; j++) {
                    int s = (t[f >> 21] * a) >> 15;
                    f += k;
                    L[j] += (s * pl) >> 8; R[j] += (s * pp) >> 8; W[j] += (s * pw) >> 8;
                }
                g->faza = f;
            } else {
                for (int j = 0; j < n; j++) {
                    g->los = g->los * 1664525u + 1013904223u;
                    int sz = (int)(g->los >> 17) - 16384;
                    g->lp += (sz - g->lp) >> 2;
                    int m = 32768;
                    if (g->lfo_krok) { g->lfo += g->lfo_krok; m = 19661 + ((SIN_T[g->lfo >> 21] * 13107) / 30000); }
                    int s = (((g->lp * 5) * a) >> 15) * m >> 15;
                    L[j] += (s * pl) >> 8; R[j] += (s * pp) >> 8; W[j] += (s * pw) >> 8;
                }
            }
        }
        for (int j = 0; j < n; j++) {
            int w = W[j] >> 3;
            int wl = poglos(0, w), wr = poglos(1, w);
            int l = L[j] + (wl >> 1), r = R[j] + (wr >> 1);
            out[2 * j]     = (short)miekko((l * glosnosc) >> 8);
            out[2 * j + 1] = (short)miekko((r * glosnosc) >> 8);
        }
        out += 2 * n; ramek -= n; probka += (u32)n;
    }
}
