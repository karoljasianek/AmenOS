/* AmenOS - dekodery obrazków: PNG, GIF, JPEG (baseline), BMP.
 * Tylko arytmetyka całkowitoliczbowa (jądro nie używa FPU). Wynik: piksele 0x00RRGGBB,
 * przezroczystość nałożona na białe tło. Cała pamięć pochodzi z areny (bez zwalniania). */
#include "obrazy.h"

const char *obraz_blad = "";

static void *arena_wez(arena_t *a, u32 n) {
    n = (n + 15u) & ~15u;
    if (a->zajete + n > a->rozmiar) { obraz_blad = "za mało pamięci na obrazek"; return 0; }
    void *p = a->pam + a->zajete;
    a->zajete += n;
    return p;
}

static u32 na_bialym(u32 r, u32 g, u32 b, u32 a) {
    r = (r * a + 255 * (255 - a)) / 255;
    g = (g * a + 255 * (255 - a)) / 255;
    b = (b * a + 255 * (255 - a)) / 255;
    return (r << 16) | (g << 8) | b;
}

#define MAKS_PIKSELI (2048u * 2048u)

/* ====================================================================== */
/*  inflate (RFC 1951)                                                     */
/* ====================================================================== */

typedef struct { const u8 *we; u32 we_n, we_poz; u32 bufor; int nbit; u8 *wy; u32 wy_n, wy_poz; int blad; } inf_t;
typedef struct { short licz[16]; short sym[320]; } huff_t;

static int bity(inf_t *s, int ile) {
    u32 v = s->bufor;
    while (s->nbit < ile) {
        if (s->we_poz >= s->we_n) { s->blad = 1; return 0; }
        v |= (u32)s->we[s->we_poz++] << s->nbit;
        s->nbit += 8;
    }
    s->bufor = v >> ile;
    s->nbit -= ile;
    return (int)(v & ((1u << ile) - 1));
}

static int dekoduj_h(inf_t *s, const huff_t *h) {
    int kod = 0, pierwszy = 0, indeks = 0;
    for (int dl = 1; dl < 16; dl++) {
        kod |= bity(s, 1);
        if (s->blad) return -1;
        int ile = h->licz[dl];
        if (kod - ile < pierwszy) return h->sym[indeks + (kod - pierwszy)];
        indeks += ile; pierwszy += ile;
        pierwszy <<= 1; kod <<= 1;
    }
    s->blad = 1;
    return -1;
}

static int zbuduj(huff_t *h, const short *dlug, int n) {
    short offs[16];
    for (int i = 0; i < 16; i++) h->licz[i] = 0;
    for (int i = 0; i < n; i++) h->licz[dlug[i]]++;
    if (h->licz[0] == n) return 0;
    int zostalo = 1;
    for (int i = 1; i < 16; i++) { zostalo <<= 1; zostalo -= h->licz[i]; if (zostalo < 0) return -1; }
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (short)(offs[i] + h->licz[i]);
    for (int i = 0; i < n; i++) if (dlug[i]) h->sym[offs[dlug[i]]++] = (short)i;
    return zostalo;
}

static const short LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                                 4097, 6145, 8193, 12289, 16385, 24577 };
static const short DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int kody(inf_t *s, const huff_t *hdl, const huff_t *hodl) {
    for (;;) {
        int sym = dekoduj_h(s, hdl);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (s->wy_poz >= s->wy_n) return -1;
            s->wy[s->wy_poz++] = (u8)sym;
        } else if (sym == 256) return 0;
        else {
            sym -= 257;
            if (sym >= 29) return -1;
            u32 dl = (u32)(LBASE[sym] + bity(s, LEXT[sym]));
            int ds = dekoduj_h(s, hodl);
            if (ds < 0 || ds >= 30) return -1;
            u32 odl = (u32)(DBASE[ds] + bity(s, DEXT[ds]));
            if (s->blad || odl > s->wy_poz || s->wy_poz + dl > s->wy_n) return -1;
            for (; dl; dl--, s->wy_poz++) s->wy[s->wy_poz] = s->wy[s->wy_poz - odl];
        }
    }
}

static huff_t h_dl, h_odl;

static int inflate(const u8 *we, u32 we_n, u8 *wy, u32 wy_n) {
    inf_t s;
    memset(&s, 0, sizeof s);
    s.we = we; s.we_n = we_n; s.wy = wy; s.wy_n = wy_n;
    int ostatni;
    do {
        ostatni = bity(&s, 1);
        int typ = bity(&s, 2);
        if (s.blad) return -1;
        if (typ == 0) {                                          /* bez kompresji */
            s.bufor = 0; s.nbit = 0;
            if (s.we_poz + 4 > s.we_n) return -1;
            u32 n = (u32)(s.we[s.we_poz] | s.we[s.we_poz + 1] << 8);
            s.we_poz += 4;
            if (s.we_poz + n > s.we_n || s.wy_poz + n > s.wy_n) return -1;
            memcpy(s.wy + s.wy_poz, s.we + s.we_poz, n);
            s.we_poz += n; s.wy_poz += n;
        } else if (typ == 1) {                                   /* stałe kody */
            short dl[320];
            int i = 0;
            for (; i < 144; i++) dl[i] = 8;
            for (; i < 256; i++) dl[i] = 9;
            for (; i < 280; i++) dl[i] = 7;
            for (; i < 288; i++) dl[i] = 8;
            zbuduj(&h_dl, dl, 288);
            for (i = 0; i < 30; i++) dl[i] = 5;
            zbuduj(&h_odl, dl, 30);
            if (kody(&s, &h_dl, &h_odl)) return -1;
        } else if (typ == 2) {                                   /* kody dynamiczne */
            static const u8 kolejnosc[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            short dl[320];
            int nl = bity(&s, 5) + 257, nd = bity(&s, 5) + 1, nk = bity(&s, 4) + 4;
            if (nl > 286 || nd > 30) return -1;
            for (int i = 0; i < 19; i++) dl[kolejnosc[i]] = (short)(i < nk ? bity(&s, 3) : 0);
            if (zbuduj(&h_dl, dl, 19) != 0) return -1;
            int i = 0;
            while (i < nl + nd) {
                int sym = dekoduj_h(&s, &h_dl), n, v = 0;
                if (sym < 0) return -1;
                if (sym < 16) { dl[i++] = (short)sym; continue; }
                if (sym == 16) { if (!i) return -1; v = dl[i - 1]; n = 3 + bity(&s, 2); }
                else if (sym == 17) n = 3 + bity(&s, 3);
                else n = 11 + bity(&s, 7);
                if (i + n > nl + nd) return -1;
                while (n--) dl[i++] = (short)v;
            }
            if (zbuduj(&h_dl, dl, nl) < 0 || zbuduj(&h_odl, dl + nl, nd) < 0) return -1;
            if (kody(&s, &h_dl, &h_odl)) return -1;
        } else return -1;
    } while (!ostatni);
    return (int)s.wy_poz;
}

/* ====================================================================== */
/*  PNG                                                                    */
/* ====================================================================== */

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
}

static int png(const u8 *d, u32 n, arena_t *a, u32 **wynik, int *ow, int *oh) {
    u32 w = 0, h = 0, idat = 0;
    int glebia = 0, typ = 0, przeplot = 0;
    u8 paleta[256 * 4];
    for (int i = 0; i < 256; i++) { paleta[i * 4] = paleta[i * 4 + 1] = paleta[i * 4 + 2] = 0; paleta[i * 4 + 3] = 255; }
    for (u32 p = 8; p + 12 <= n;) {                              /* pierwsze przejście: nagłówek i rozmiar danych */
        u32 dl = be32(d + p);
        const u8 *t = d + p + 4, *c = d + p + 8;
        if (p + 12 + dl > n) break;
        if (!k_memcmp(t, "IHDR", 4)) { w = be32(c); h = be32(c + 4); glebia = c[8]; typ = c[9]; przeplot = c[12]; }
        else if (!k_memcmp(t, "PLTE", 4)) for (u32 i = 0; i < dl / 3 && i < 256; i++) memcpy(paleta + i * 4, c + i * 3, 3);
        else if (!k_memcmp(t, "tRNS", 4) && typ == 3) for (u32 i = 0; i < dl && i < 256; i++) paleta[i * 4 + 3] = c[i];
        else if (!k_memcmp(t, "IDAT", 4)) idat += dl;
        p += 12 + dl;
    }
    if (!w || !h || w * h > MAKS_PIKSELI || w > 4096 || h > 4096) { obraz_blad = "nieprawidłowy lub za duży PNG"; return 0; }
    if (przeplot) { obraz_blad = "PNG z przeplotem nie jest obsługiwany"; return 0; }
    int kan = typ == 0 ? 1 : typ == 2 ? 3 : typ == 3 ? 1 : typ == 4 ? 2 : typ == 6 ? 4 : 0;
    if (!kan || (glebia != 1 && glebia != 2 && glebia != 4 && glebia != 8 && glebia != 16)) { obraz_blad = "nieobsługiwany rodzaj PNG"; return 0; }
    u8 *z = arena_wez(a, idat);
    if (!z) return 0;
    u32 zp = 0;
    for (u32 p = 8; p + 12 <= n;) {
        u32 dl = be32(d + p);
        if (p + 12 + dl > n) break;
        if (!k_memcmp(d + p + 4, "IDAT", 4)) { memcpy(z + zp, d + p + 8, dl); zp += dl; }
        p += 12 + dl;
    }
    u32 wiersz = (w * (u32)kan * (u32)glebia + 7) / 8;
    u32 surowe_n = h * (wiersz + 1);
    u8 *sur = arena_wez(a, surowe_n);
    u32 *piks = arena_wez(a, w * h * 4);
    if (!sur || !piks) return 0;
    if (idat < 2 || inflate(z + 2, idat - 2, sur, surowe_n) < (int)surowe_n) { obraz_blad = "uszkodzone dane PNG"; return 0; }
    u32 bpp = (u32)(kan * glebia) / 8;
    if (!bpp) bpp = 1;
    for (u32 y = 0; y < h; y++) {                                /* filtry wierszy */
        u8 *r = sur + y * (wiersz + 1) + 1, *pr = y ? r - (wiersz + 1) : 0;
        u8 f = r[-1];
        for (u32 x = 0; x < wiersz; x++) {
            int lewy = x >= bpp ? r[x - bpp] : 0, gora = pr ? pr[x] : 0, lg = (pr && x >= bpp) ? pr[x - bpp] : 0;
            if (f == 1) r[x] = (u8)(r[x] + lewy);
            else if (f == 2) r[x] = (u8)(r[x] + gora);
            else if (f == 3) r[x] = (u8)(r[x] + ((lewy + gora) >> 1));
            else if (f == 4) r[x] = (u8)(r[x] + paeth(lewy, gora, lg));
        }
    }
    int krok = glebia == 16 ? 2 : 1;
    for (u32 y = 0; y < h; y++) {
        const u8 *r = sur + y * (wiersz + 1) + 1;
        for (u32 x = 0; x < w; x++) {
            u32 c;
            if (glebia < 8) {
                u32 bit = x * (u32)glebia;
                u32 v = (u32)(r[bit >> 3] >> (8 - glebia - (bit & 7))) & ((1u << glebia) - 1);
                if (typ == 3) c = na_bialym(paleta[v * 4], paleta[v * 4 + 1], paleta[v * 4 + 2], paleta[v * 4 + 3]);
                else { v = v * 255 / ((1u << glebia) - 1); c = v << 16 | v << 8 | v; }
            } else {
                const u8 *q = r + x * (u32)kan * (u32)krok;
                if (typ == 0) c = (u32)q[0] * 0x010101u;
                else if (typ == 2) c = (u32)q[0] << 16 | (u32)q[krok] << 8 | q[2 * krok];
                else if (typ == 3) c = na_bialym(paleta[q[0] * 4], paleta[q[0] * 4 + 1], paleta[q[0] * 4 + 2], paleta[q[0] * 4 + 3]);
                else if (typ == 4) c = na_bialym(q[0], q[0], q[0], q[krok]);
                else c = na_bialym(q[0], q[krok], q[2 * krok], q[3 * krok]);
            }
            piks[y * w + x] = c;
        }
    }
    *wynik = piks; *ow = (int)w; *oh = (int)h;
    return 1;
}

/* ====================================================================== */
/*  GIF (pierwsza klatka)                                                  */
/* ====================================================================== */

static u16 lzw_pref[4096];
static u8 lzw_suf[4096], lzw_stos[4097];

static int gif(const u8 *d, u32 n, arena_t *a, u32 **wynik, int *ow, int *oh) {
    if (n < 13) return 0;
    int w = d[6] | d[7] << 8, h = d[8] | d[9] << 8;
    if (w <= 0 || h <= 0 || (u32)(w * h) > MAKS_PIKSELI) { obraz_blad = "nieprawidłowy GIF"; return 0; }
    u8 paleta_g[768];
    int ng = 0;
    u32 p = 13;
    if (d[10] & 0x80) { ng = 2 << (d[10] & 7); if (p + (u32)ng * 3 > n) return 0; memcpy(paleta_g, d + p, (u32)ng * 3); p += (u32)ng * 3; }
    int przezr = -1;
    u32 *piks = arena_wez(a, (u32)(w * h) * 4);
    if (!piks) return 0;
    for (int i = 0; i < w * h; i++) piks[i] = 0xFFFFFF;
    while (p < n) {
        u8 b = d[p++];
        if (b == 0x21 && p < n) {                                /* rozszerzenia */
            u8 et = d[p++];
            if (et == 0xF9 && p + 5 < n && d[p] == 4 && (d[p + 1] & 1)) przezr = d[p + 4];
            while (p < n && d[p]) p += d[p] + 1u;
            p++;
        } else if (b == 0x2C) {                                  /* obraz */
            if (p + 9 > n) return 0;
            int ix = d[p] | d[p + 1] << 8, iy = d[p + 2] | d[p + 3] << 8, iw = d[p + 4] | d[p + 5] << 8, ih = d[p + 6] | d[p + 7] << 8;
            u8 fl = d[p + 8];
            p += 9;
            const u8 *pal = paleta_g;
            u8 paleta_l[768];
            if (fl & 0x80) { int nl = 2 << (fl & 7); if (p + (u32)nl * 3 > n) return 0; memcpy(paleta_l, d + p, (u32)nl * 3); pal = paleta_l; p += (u32)nl * 3; }
            if (p >= n || iw <= 0 || ih <= 0 || (u32)(iw * ih) > MAKS_PIKSELI) return 0;
            int min = d[p++];
            if (min < 2 || min > 8) return 0;
            u32 rozm = 0;                                        /* złącz pod-bloki */
            for (u32 q = p; q < n && d[q]; q += d[q] + 1u) rozm += d[q];
            u8 *dane = arena_wez(a, rozm + 4);
            u8 *ind = arena_wez(a, (u32)(iw * ih));
            if (!dane || !ind) return 0;
            u32 dp = 0;
            while (p < n && d[p]) { u32 l = d[p]; if (p + 1 + l > n) break; memcpy(dane + dp, d + p + 1, l); dp += l; p += l + 1; }
            p++;
            memset(ind, 0, (u32)(iw * ih));
            /* LZW */
            int czysc = 1 << min, koniec = czysc + 1, wolny = czysc + 2, rozmiar = min + 1, stary = -1, pierwszy = 0;
            for (int i = 0; i < czysc; i++) { lzw_suf[i] = (u8)i; lzw_pref[i] = 0; }
            u32 bufb = 0; int nb = 0; u32 bp = 0, op = 0, maks = (u32)(iw * ih);
            for (;;) {
                while (nb < rozmiar && bp < dp) { bufb |= (u32)dane[bp++] << nb; nb += 8; }
                if (nb < rozmiar) break;
                int kod = (int)(bufb & ((1u << rozmiar) - 1));
                bufb >>= rozmiar; nb -= rozmiar;
                if (kod == czysc) { rozmiar = min + 1; wolny = czysc + 2; stary = -1; continue; }
                if (kod == koniec) break;
                if (stary < 0) {
                    if (kod >= czysc) break;
                    if (op < maks) ind[op++] = (u8)kod;
                    stary = pierwszy = kod;
                    continue;
                }
                int wej = kod, sp = 0;
                if (kod >= wolny) { if (kod > wolny) break; lzw_stos[sp++] = (u8)pierwszy; kod = stary; }
                while (kod >= czysc && sp < 4096) { lzw_stos[sp++] = lzw_suf[kod]; kod = lzw_pref[kod]; }
                pierwszy = lzw_suf[kod];
                lzw_stos[sp++] = (u8)pierwszy;
                if (wolny < 4096) {
                    lzw_pref[wolny] = (u16)stary; lzw_suf[wolny] = (u8)pierwszy; wolny++;
                    if (wolny == (1 << rozmiar) && rozmiar < 12) rozmiar++;
                }
                while (sp && op < maks) ind[op++] = lzw_stos[--sp];
                stary = wej;
            }
            /* przeplot */
            static const int start[4] = { 0, 4, 2, 1 }, krok[4] = { 8, 8, 4, 2 };
            int wiersz_src = 0;
            for (int pass = 0; pass < ((fl & 0x40) ? 4 : 1); pass++)
                for (int y = (fl & 0x40) ? start[pass] : 0; y < ih; y += (fl & 0x40) ? krok[pass] : 1, wiersz_src++)
                    for (int x = 0; x < iw; x++) {
                        int c = ind[wiersz_src * iw + x], cx = ix + x, cy = iy + y;
                        if (c == przezr || cx >= w || cy >= h) continue;
                        piks[cy * w + cx] = (u32)pal[c * 3] << 16 | (u32)pal[c * 3 + 1] << 8 | pal[c * 3 + 2];
                    }
            *wynik = piks; *ow = w; *oh = h;
            return 1;
        } else break;
    }
    obraz_blad = "GIF bez obrazu";
    return 0;
}

/* ====================================================================== */
/*  JPEG (baseline, Huffman)                                               */
/* ====================================================================== */

typedef struct { u8 val[256]; int mincode[17], maxcode[18], valptr[17]; int jest; } jhuff_t;
typedef struct { int id, H, V, tq, td, ta, pred, stride; u8 *plan; } jkomp_t;

static jhuff_t jdc[4], jac[4];
static u16 jq[4][64];
static jkomp_t jk[3];
static int jnk, jw, jh, jri;
static const u8 *jd; static u32 jn, jp;
static u32 jbuf; static int jnb, jmarker;

static const u8 ZIGZAG[64] = { 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

static int jbit(void) {
    if (!jnb) {
        u8 b = 0;
        if (!jmarker && jp < jn) {
            b = jd[jp++];
            if (b == 0xFF) {
                u8 c = jp < jn ? jd[jp] : 0;
                if (c == 0) jp++;
                else { jmarker = 1; b = 0; jp--; }               /* znacznik (RSTn/EOI) - dalej same zera */
            }
        }
        jbuf = b; jnb = 8;
    }
    jnb--;
    return (int)((jbuf >> jnb) & 1);
}

static int jbity(int n) { int v = 0; while (n--) v = (v << 1) | jbit(); return v; }

static int jdekoduj(const jhuff_t *h) {
    int kod = 0;
    for (int l = 1; l <= 16; l++) {
        kod = (kod << 1) | jbit();
        if (h->maxcode[l] >= 0 && kod <= h->maxcode[l]) return h->val[h->valptr[l] + kod - h->mincode[l]];
    }
    return -1;
}

static int jrozszerz(int v, int s) { return (s && v < (1 << (s - 1))) ? v - (1 << s) + 1 : v; }

#define W1 2841
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

static u8 jklip(int x) { return (u8)(x < 0 ? 0 : x > 255 ? 255 : x); }

static void idct_wiersz(int *b) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = b[4] << 11) | (x2 = b[6]) | (x3 = b[2]) | (x4 = b[1]) | (x5 = b[7]) | (x6 = b[5]) | (x7 = b[3]))) {
        b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = b[0] << 3;
        return;
    }
    x0 = (b[0] << 11) + 128;
    x8 = W7 * (x4 + x5); x4 = x8 + (W1 - W7) * x4; x5 = x8 - (W1 + W7) * x5;
    x8 = W3 * (x6 + x7); x6 = x8 - (W3 - W5) * x6; x7 = x8 - (W3 + W5) * x7;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2); x2 = x1 - (W2 + W6) * x2; x3 = x1 + (W2 - W6) * x3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    b[0] = (x7 + x1) >> 8; b[1] = (x3 + x2) >> 8; b[2] = (x0 + x4) >> 8; b[3] = (x8 + x6) >> 8;
    b[4] = (x8 - x6) >> 8; b[5] = (x0 - x4) >> 8; b[6] = (x3 - x2) >> 8; b[7] = (x7 - x1) >> 8;
}

static void idct_kolumna(const int *b, u8 *o, int s) {
    int x0, x1, x2, x3, x4, x5, x6, x7, x8;
    if (!((x1 = b[32] << 8) | (x2 = b[48]) | (x3 = b[16]) | (x4 = b[8]) | (x5 = b[56]) | (x6 = b[40]) | (x7 = b[24]))) {
        u8 v = jklip(((b[0] + 32) >> 6) + 128);
        for (int i = 0; i < 8; i++) o[i * s] = v;
        return;
    }
    x0 = (b[0] << 8) + 8192;
    x8 = W7 * (x4 + x5) + 4; x4 = (x8 + (W1 - W7) * x4) >> 3; x5 = (x8 - (W1 + W7) * x5) >> 3;
    x8 = W3 * (x6 + x7) + 4; x6 = (x8 - (W3 - W5) * x6) >> 3; x7 = (x8 - (W3 + W5) * x7) >> 3;
    x8 = x0 + x1; x0 -= x1;
    x1 = W6 * (x3 + x2) + 4; x2 = (x1 - (W2 + W6) * x2) >> 3; x3 = (x1 + (W2 - W6) * x3) >> 3;
    x1 = x4 + x6; x4 -= x6; x6 = x5 + x7; x5 -= x7;
    x7 = x8 + x3; x8 -= x3; x3 = x0 + x2; x0 -= x2;
    x2 = (181 * (x4 + x5) + 128) >> 8; x4 = (181 * (x4 - x5) + 128) >> 8;
    o[0] = jklip(((x7 + x1) >> 14) + 128); o[s] = jklip(((x3 + x2) >> 14) + 128);
    o[2 * s] = jklip(((x0 + x4) >> 14) + 128); o[3 * s] = jklip(((x8 + x6) >> 14) + 128);
    o[4 * s] = jklip(((x8 - x6) >> 14) + 128); o[5 * s] = jklip(((x0 - x4) >> 14) + 128);
    o[6 * s] = jklip(((x3 - x2) >> 14) + 128); o[7 * s] = jklip(((x7 - x1) >> 14) + 128);
}

static int jblok(jkomp_t *k, u8 *o) {
    int b[64];
    memset(b, 0, sizeof b);
    int s = jdekoduj(&jdc[k->td]);
    if (s < 0) return 0;
    k->pred += s ? jrozszerz(jbity(s), s) : 0;
    b[0] = k->pred * jq[k->tq][0];
    for (int i = 1; i < 64;) {
        int rs = jdekoduj(&jac[k->ta]);
        if (rs < 0) return 0;
        int r = rs >> 4, v = rs & 15;
        if (!v) { if (r != 15) break; i += 16; continue; }
        i += r;
        if (i > 63) break;
        b[ZIGZAG[i]] = jrozszerz(jbity(v), v) * jq[k->tq][i];
        i++;
    }
    for (int i = 0; i < 64; i += 8) idct_wiersz(b + i);
    for (int i = 0; i < 8; i++) idct_kolumna(b + i, o + i, k->stride);
    return 1;
}

/* kolor w punkcie (x, y) - interpolacja liniowa podpróbkowanej płaszczyzny (środki próbek) */
static int chroma(const jkomp_t *k, int x, int y, int hm, int vm) {
    int fx = hm / k->H, fy = vm / k->V;
    if (fx == 1 && fy == 1) return k->plan[y * k->stride + x];
    int szer = (jw * k->H + hm - 1) / hm, wys = (jh * k->V + vm - 1) / vm;
    int nx = 2 * x + 1 - fx, dx = 2 * fx, ny = 2 * y + 1 - fy, dy = 2 * fy;
    int x0 = nx >= 0 ? nx / dx : -1, y0 = ny >= 0 ? ny / dy : -1;
    int ax = nx - x0 * dx, ay = ny - y0 * dy;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= szer) x1 = szer - 1;
    if (y1 >= wys) y1 = wys - 1;
    const u8 *p = k->plan;
    int s = k->stride;
    int g = p[y0 * s + x0] * (dx - ax) + p[y0 * s + x1] * ax;
    int d = p[y1 * s + x0] * (dx - ax) + p[y1 * s + x1] * ax;
    return (g * (dy - ay) + d * ay + (dx * dy) / 2) / (dx * dy);
}

static int jpeg(const u8 *d, u32 n, arena_t *a, u32 **wynik, int *ow, int *oh) {
    jd = d; jn = n; jri = 0; jnk = 0; jw = jh = 0;
    for (int i = 0; i < 4; i++) { jdc[i].jest = jac[i].jest = 0; }
    u32 p = 2;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        u8 m = d[p + 1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { p += 2; continue; }
        if (m == 0xD9) break;
        u32 dl = (u32)(d[p + 2] << 8 | d[p + 3]);
        const u8 *s = d + p + 4;
        if (p + 2 + dl > n) break;
        if (m == 0xC0 || m == 0xC1) {
            if (s[0] != 8) { obraz_blad = "JPEG o głębi innej niż 8 bitów"; return 0; }
            jh = s[1] << 8 | s[2]; jw = s[3] << 8 | s[4]; jnk = s[5];
            if (jnk != 1 && jnk != 3) { obraz_blad = "JPEG CMYK nie jest obsługiwany"; return 0; }
            for (int i = 0; i < jnk; i++) { jk[i].id = s[6 + i * 3]; jk[i].H = s[7 + i * 3] >> 4; jk[i].V = s[7 + i * 3] & 15; jk[i].tq = s[8 + i * 3] & 3; }
        } else if (m == 0xC2 || m == 0xC6 || m == 0xCA) { obraz_blad = "JPEG progresywny nie jest obsługiwany"; return 0; }
        else if (m == 0xC4) {
            u32 q = 0;
            while (q + 17 <= dl - 2) {
                int tc = s[q] >> 4, th = s[q] & 3;
                jhuff_t *h = tc ? &jac[th] : &jdc[th];
                const u8 *ile = s + q + 1;
                int suma = 0;
                for (int i = 0; i < 16; i++) suma += ile[i];
                if (suma > 256 || q + 17 + (u32)suma > dl - 2) break;
                memcpy(h->val, s + q + 17, (u32)suma);
                int kod = 0, k = 0;
                for (int l = 1; l <= 16; l++) {
                    h->valptr[l] = k; h->mincode[l] = kod;
                    kod += ile[l - 1]; k += ile[l - 1];
                    h->maxcode[l] = ile[l - 1] ? kod - 1 : -1;
                    kod <<= 1;
                }
                h->jest = 1;
                q += 17 + (u32)suma;
            }
        } else if (m == 0xDB) {
            u32 q = 0;
            while (q < dl - 2) {
                int pq = s[q] >> 4, tq = s[q] & 3;
                q++;
                for (int i = 0; i < 64; i++) jq[tq][i] = pq ? (u16)(s[q + 2 * i] << 8 | s[q + 2 * i + 1]) : s[q + i];
                q += pq ? 128 : 64;
            }
        } else if (m == 0xDD) jri = s[0] << 8 | s[1];
        else if (m == 0xDA) {
            if (!jw || !jh) return 0;
            if ((u32)(jw * jh) > MAKS_PIKSELI) { obraz_blad = "za duży JPEG"; return 0; }
            int ns = s[0];
            for (int i = 0; i < ns; i++)
                for (int c = 0; c < jnk; c++)
                    if (jk[c].id == s[1 + i * 2]) { jk[c].td = s[2 + i * 2] >> 4 & 3; jk[c].ta = s[2 + i * 2] & 3; }
            int hm = 1, vm = 1;
            for (int c = 0; c < jnk; c++) { if (jk[c].H > hm) hm = jk[c].H; if (jk[c].V > vm) vm = jk[c].V; }
            int mx = (jw + 8 * hm - 1) / (8 * hm), my = (jh + 8 * vm - 1) / (8 * vm);
            if (jnk == 1) { mx = (jw + 7) / 8; my = (jh + 7) / 8; jk[0].H = jk[0].V = 1; hm = vm = 1; }
            for (int c = 0; c < jnk; c++) {
                if (!jdc[jk[c].td].jest || !jac[jk[c].ta].jest) { obraz_blad = "JPEG bez tablic Huffmana"; return 0; }
                jk[c].stride = mx * jk[c].H * 8;
                jk[c].plan = arena_wez(a, (u32)(jk[c].stride * my * jk[c].V * 8));
                if (!jk[c].plan) return 0;
                jk[c].pred = 0;
            }
            jp = p + 2 + dl; jnb = 0; jmarker = 0;
            int licz = 0;
            for (int y = 0; y < my; y++)
                for (int x = 0; x < mx; x++) {
                    if (jri && licz && licz % jri == 0) {        /* znacznik restartu */
                        jnb = 0; jmarker = 0;
                        while (jp + 1 < jn && !(jd[jp] == 0xFF && jd[jp + 1] >= 0xD0 && jd[jp + 1] <= 0xD7)) jp++;
                        jp += 2;
                        for (int c = 0; c < jnk; c++) jk[c].pred = 0;
                    }
                    licz++;
                    for (int c = 0; c < jnk; c++)
                        for (int v = 0; v < jk[c].V; v++)
                            for (int h = 0; h < jk[c].H; h++) {
                                u8 *o = jk[c].plan + ((y * jk[c].V + v) * 8) * jk[c].stride + (x * jk[c].H + h) * 8;
                                if (!jblok(&jk[c], o)) { obraz_blad = "uszkodzone dane JPEG"; return 0; }
                            }
                }
            u32 *piks = arena_wez(a, (u32)(jw * jh) * 4);
            if (!piks) return 0;
            for (int y = 0; y < jh; y++)
                for (int x = 0; x < jw; x++) {
                    int Y = jk[0].plan[y * jk[0].stride + x];
                    if (jnk == 1) { piks[y * jw + x] = (u32)Y * 0x010101u; continue; }
                    int cb = chroma(&jk[1], x, y, hm, vm) - 128;
                    int cr = chroma(&jk[2], x, y, hm, vm) - 128;
                    int r = Y + ((91881 * cr) >> 16), g = Y - ((22554 * cb + 46802 * cr) >> 16), b = Y + ((116130 * cb) >> 16);
                    piks[y * jw + x] = (u32)jklip(r) << 16 | (u32)jklip(g) << 8 | jklip(b);
                }
            *wynik = piks; *ow = jw; *oh = jh;
            return 1;
        }
        p += 2 + dl;
    }
    obraz_blad = "JPEG bez danych obrazu";
    return 0;
}

/* ====================================================================== */
/*  BMP                                                                    */
/* ====================================================================== */

static int bmp(const u8 *d, u32 n, arena_t *a, u32 **wynik, int *ow, int *oh) {
    if (n < 54) return 0;
    u32 off = d[10] | d[11] << 8 | (u32)d[12] << 16 | (u32)d[13] << 24, hs = d[14] | d[15] << 8;
    int w = (int)(d[18] | d[19] << 8 | d[20] << 16 | (u32)d[21] << 24), h = (int)(d[22] | d[23] << 8 | d[24] << 16 | (u32)d[25] << 24);
    int bpp = d[28] | d[29] << 8, komp = d[30];
    int odwr = h > 0;
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || (u32)(w * h) > MAKS_PIKSELI || (komp != 0 && !(komp == 3 && bpp == 32))) { obraz_blad = "nieobsługiwany BMP"; return 0; }
    if (bpp != 24 && bpp != 32 && bpp != 8) { obraz_blad = "nieobsługiwany BMP"; return 0; }
    u32 wiersz = ((u32)(w * bpp) / 8 + 3) & ~3u;
    if (off + wiersz * (u32)h > n) return 0;
    const u8 *pal = d + 14 + hs;
    u32 *piks = arena_wez(a, (u32)(w * h) * 4);
    if (!piks) return 0;
    for (int y = 0; y < h; y++) {
        const u8 *r = d + off + wiersz * (u32)(odwr ? h - 1 - y : y);
        for (int x = 0; x < w; x++) {
            const u8 *q = bpp == 8 ? pal + r[x] * 4 : r + x * (bpp / 8);
            piks[y * w + x] = (u32)q[2] << 16 | (u32)q[1] << 8 | q[0];
        }
    }
    *wynik = piks; *ow = w; *oh = h;
    return 1;
}

/* ====================================================================== */

int obraz_dekoduj(const u8 *d, u32 n, arena_t *a, u32 **piks, int *w, int *h) {
    obraz_blad = "nieznany format obrazka";
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return png(d, n, a, piks, w, h);
    if (n >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return gif(d, n, a, piks, w, h);
    if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) return jpeg(d, n, a, piks, w, h);
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return bmp(d, n, a, piks, w, h);
    return 0;
}

/* zmniejszanie przez uśrednianie (powiększanie - najbliższy sąsiad) */
void obraz_skaluj(const u32 *z, int zw, int zh, u32 *d, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        int y0 = y * zh / dh, y1 = (y + 1) * zh / dh;
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = x * zw / dw, x1 = (x + 1) * zw / dw;
            if (x1 <= x0) x1 = x0 + 1;
            u32 r = 0, g = 0, b = 0, ile = 0;
            for (int j = y0; j < y1; j++)
                for (int i = x0; i < x1; i++) {
                    u32 c = z[j * zw + i];
                    r += c >> 16 & 0xFF; g += c >> 8 & 0xFF; b += c & 0xFF; ile++;
                }
            d[y * dw + x] = (r / ile) << 16 | (g / ile) << 8 | (b / ile);
        }
    }
}
