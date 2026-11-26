/* AmenOS - grafika: bufor ramki ustawiony przez GRUB (VBE) i podstawowe rysowanie.
 * Rysujemy do bufora w pamięci RAM, a gotowe fragmenty kopiujemy na ekran. */
#include "kernel.h"
#include "grafika_dane.h"
#include "wyglad_dane.h"

int fb_jest;
int fb_w, fb_h;
static u8 *fb;
static u32 fb_pitch, fb_bpp;
static u32 *bb;                              /* bufor w RAM, format 0x00RRGGBB */
static int poz_r, poz_g, poz_b;
static int kx0, ky0, kx1, ky1;               /* prostokąt przycinania (x1, y1 wyłącznie) */

int fb_init(const u32 *mbi) {
    if (!(mbi[0] & (1u << 12))) return 0;
    const u8 *m = (const u8 *)mbi;
    u32 adr = *(const u32 *)(m + 88), adr_hi = *(const u32 *)(m + 92);
    fb_pitch = *(const u32 *)(m + 96);
    fb_w = (int)*(const u32 *)(m + 100);
    fb_h = (int)*(const u32 *)(m + 104);
    fb_bpp = m[108];
    u8 typ = m[109];
    if (adr_hi || typ != 1 || (fb_bpp != 32 && fb_bpp != 24) || fb_w < 800 || fb_h < 600 || fb_w > 2560 || fb_h > 1600)
        return 0;
    poz_r = m[112]; poz_g = m[114]; poz_b = m[116];      /* unia color_info jest wyrównana do 4 bajtów */
    bb = pamiec_duza((u32)fb_w * (u32)fb_h * 4u);
    if (!bb) return 0;                                       /* za mało pamięci */
    fb = (u8 *)adr;
    fb_klip_caly();
    fb_jest = 1;
    return 1;
}

void fb_klip(int x, int y, int w, int h) {
    kx0 = x < 0 ? 0 : x; ky0 = y < 0 ? 0 : y;
    kx1 = x + w > fb_w ? fb_w : x + w; ky1 = y + h > fb_h ? fb_h : y + h;
}
void fb_klip_caly(void) { kx0 = 0; ky0 = 0; kx1 = fb_w; ky1 = fb_h; }
void fb_klip_zawez(int x, int y, int w, int h) {
    if (x > kx0) kx0 = x;
    if (y > ky0) ky0 = y;
    if (x + w < kx1) kx1 = x + w;
    if (y + h < ky1) ky1 = y + h;
}
void fb_klip_pobierz(int *x, int *y, int *w, int *h) { *x = kx0; *y = ky0; *w = kx1 - kx0; *h = ky1 - ky0; }

void fb_piksel(int x, int y, u32 c) {
    if (x >= kx0 && x < kx1 && y >= ky0 && y < ky1) bb[y * fb_w + x] = c;
}

u32 fb_mieszaj(u32 a, u32 b, int alfa) {          /* alfa 0..256: udział koloru b */
    u32 r = (((a >> 16) & 0xFF) * (u32)(256 - alfa) + ((b >> 16) & 0xFF) * (u32)alfa) >> 8;
    u32 g = (((a >> 8) & 0xFF) * (u32)(256 - alfa) + ((b >> 8) & 0xFF) * (u32)alfa) >> 8;
    u32 l = ((a & 0xFF) * (u32)(256 - alfa) + (b & 0xFF) * (u32)alfa) >> 8;
    return (r << 16) | (g << 8) | l;
}

void fb_prost(int x, int y, int w, int h, u32 c) {
    int x0 = x < kx0 ? kx0 : x, y0 = y < ky0 ? ky0 : y;
    int x1 = x + w > kx1 ? kx1 : x + w, y1 = y + h > ky1 ? ky1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *p = bb + j * fb_w;
        for (int i = x0; i < x1; i++) p[i] = c;
    }
}

void fb_prost_alfa(int x, int y, int w, int h, u32 c, int alfa) {
    int x0 = x < kx0 ? kx0 : x, y0 = y < ky0 ? ky0 : y;
    int x1 = x + w > kx1 ? kx1 : x + w, y1 = y + h > ky1 ? ky1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *p = bb + j * fb_w;
        for (int i = x0; i < x1; i++) p[i] = fb_mieszaj(p[i], c, alfa);
    }
}

void fb_ramka(int x, int y, int w, int h, u32 c) {
    fb_prost(x, y, w, 1, c); fb_prost(x, y + h - 1, w, 1, c);
    fb_prost(x, y, 1, h, c); fb_prost(x + w - 1, y, 1, h, c);
}

void fb_gradient(int x, int y, int w, int h, u32 c1, u32 c2) {
    int y0 = y < ky0 ? ky0 : y, y1 = y + h > ky1 ? ky1 : y + h;
    for (int j = y0; j < y1; j++) fb_prost(x, j, w, 1, fb_mieszaj(c1, c2, h > 1 ? (j - y) * 256 / (h - 1) : 0));
}

void fb_linia(int x0, int y0, int x1, int y1, u32 c, int grubosc) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy, r = grubosc / 2;
    for (;;) {
        if (grubosc <= 1) fb_piksel(x0, y0, c);
        else fb_prost(x0 - r, y0 - r, grubosc, grubosc, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

void fb_kolo(int cx, int cy, int r, u32 c) {
    for (int y = -r; y <= r; y++) {
        int x = 0;
        while ((x + 1) * (x + 1) + y * y <= r * r) x++;
        fb_prost(cx - x, cy + y, 2 * x + 1, 1, c);
    }
}

void fb_okrag(int cx, int cy, int r, u32 c, int grubosc) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d <= r * r && d > (r - grubosc) * (r - grubosc)) fb_piksel(cx + x, cy + y, c);
        }
}

/* prostokąt z zaokrąglonymi rogami */
void fb_prost_zaokr(int x, int y, int w, int h, int r, u32 c) {
    fb_prost(x + r, y, w - 2 * r, h, c);
    fb_prost(x, y + r, r, h - 2 * r, c);
    fb_prost(x + w - r, y + r, r, h - 2 * r, c);
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            int dx = r - i, dy = r - j;
            if (dx * dx + dy * dy <= r * r + r) {
                fb_piksel(x + i, y + j, c); fb_piksel(x + w - 1 - i, y + j, c);
                fb_piksel(x + i, y + h - 1 - j, c); fb_piksel(x + w - 1 - i, y + h - 1 - j, c);
            }
        }
}

/* znak z czcionki 8x16; tlo = FB_PRZEZR, żeby nie malować tła */
void fb_znak(int x, int y, u8 g, u32 kolor, u32 tlo, int skala) {
    if (x + 8 * skala <= kx0 || x >= kx1 || y + 16 * skala <= ky0 || y >= ky1) return;
    for (int r = 0; r < 16; r++) {
        u8 b = czcionka[g][r];
        for (int k = 0; k < 8; k++) {
            int zap = b & (0x80 >> k);
            if (!zap && tlo == FB_PRZEZR) continue;
            if (skala == 1) fb_piksel(x + k, y + r, zap ? kolor : tlo);
            else fb_prost(x + k * skala, y + r * skala, skala, skala, zap ? kolor : tlo);
        }
    }
}

int fb_tekst(int x, int y, const char *s, u32 kolor, u32 tlo, int skala) {
    int x0 = x;
    while (*s) { fb_znak(x, y, vga_map(k_utf8_next(&s)), kolor, tlo, skala); x += 8 * skala; }
    return x - x0;
}

int fb_tekst_szer(const char *s, int skala) { return (int)k_utf8_len(s) * 8 * skala; }

/* tekst z zawijaniem po słowach; zwraca liczbę wierszy */
int fb_tekst_zawin(int x, int y, const char *s, int max_znakow, u32 kolor, int odstep) {
    u32 cp[400]; int n = 0;
    while (*s && n < 400) cp[n++] = k_utf8_next(&s);
    int i = 0, linie = 0;
    while (i < n) {
        int k = i + max_znakow;
        if (k >= n) k = n;
        else { int t = k; while (t > i && cp[t] != ' ') t--; if (t > i) k = t; }
        for (int j = i; j < k; j++) fb_znak(x + (j - i) * 8, y + linie * odstep, vga_map(cp[j]), kolor, FB_PRZEZR, 1);
        linie++;
        i = k;
        while (i < n && cp[i] == ' ') i++;
    }
    return linie;
}

/* ikona 16x16 powiększona */
/* obrazek (piksele 0x00RRGGBB) z przycinaniem */
void fb_obraz(int x, int y, int w, int h, const u32 *piks) {
    int x0 = x < kx0 ? kx0 : x, x1 = x + w > kx1 ? kx1 : x + w;
    int y0 = y < ky0 ? ky0 : y, y1 = y + h > ky1 ? ky1 : y + h;
    for (int j = y0; j < y1; j++) {
        const u32 *z = piks + (j - y) * w + (x0 - x);
        u32 *d = bb + j * fb_w + x0;
        for (int i = x0; i < x1; i++) *d++ = *z++;
    }
}

void fb_ikona(int x, int y, const u32 *piks, int skala) {
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) {
            u32 c = piks[j * 16 + i];
            if (c != FB_PRZEZR) fb_prost(x + i * skala, y + j * skala, skala, skala, c);
        }
}

const u32 *fb_ikona_nr(int nr) {
    static const u32 *const tab[] = { ikona_terminal, ikona_kalendarz, ikona_zegar, ikona_piesni, ikona_muzyka,
        ikona_internet, ikona_modlitwy, ikona_spowiedz, ikona_komunia, ikona_ogloszenia, ikona_msze, ikona_system,
        ikona_kalkulator };
    return tab[nr];
}

int fb_sinus(int i) { return sinus60[((i % 60) + 60) % 60]; }

/* kopiuje fragment bufora na ekran */
void fb_wyslij(int x, int y, int w, int h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;
    int prosty = (poz_r == 16 && poz_g == 8 && poz_b == 0);
    for (int j = y; j < y + h; j++) {
        u32 *z = bb + j * fb_w + x;
        u8 *d = fb + (u32)j * fb_pitch + (u32)x * (fb_bpp / 8);
        if (fb_bpp == 32 && prosty) memcpy(d, z, (u32)w * 4u); else if (fb_bpp == 32) {
            u32 *d32 = (u32 *)d;
            for (int i = 0; i < w; i++) {
                u32 c = z[i];
                d32[i] = (((c >> 16) & 0xFF) << poz_r) | (((c >> 8) & 0xFF) << poz_g) | ((c & 0xFF) << poz_b);
            }
        } else {
            for (int i = 0; i < w; i++) {
                u32 c = z[i];
                u32 v = (((c >> 16) & 0xFF) << poz_r) | (((c >> 8) & 0xFF) << poz_g) | ((c & 0xFF) << poz_b);
                d[3 * i] = (u8)v; d[3 * i + 1] = (u8)(v >> 8); d[3 * i + 2] = (u8)(v >> 16);
            }
        }
    }
}

/* ====================================================================== */
/*  Nowoczesny wygląd: wygładzanie, przezroczystość, cienie, czcionki      */
/* ====================================================================== */

static u32 pierw(u32 v) {                       /* pierwiastek całkowity */
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static inline void mieszaj_px(u32 *p, u32 c, int a) {
    if (a >= 256) { *p = c; return; }
    if (a <= 0) return;
    *p = fb_mieszaj(*p, c, a);
}

static inline int w_klipie(int x, int y) { return x >= kx0 && x < kx1 && y >= ky0 && y < ky1; }

/* pokrycie piksela (px,py) przez koło o środku (cx16,cy16) i promieniu r16 - jednostki 1/16 piksela; wynik 0..256 */
static int pokrycie(int px, int py, int cx16, int cy16, int r16) {
    int dx = px * 16 + 8 - cx16, dy = py * 16 + 8 - cy16;
    int d = (int)pierw((u32)(dx * dx + dy * dy));
    int c = r16 - d + 8;
    if (c <= 0) return 0;
    if (c >= 16) return 256;
    return c * 16;
}

/* ---------- prostokąty z zaokrąglonymi rogami ---------- */

static void wiersz_alfa(int x0, int x1, int y, u32 c, int alfa) {
    if (y < ky0 || y >= ky1) return;
    if (x0 < kx0) x0 = kx0;
    if (x1 > kx1) x1 = kx1;
    u32 *p = bb + y * fb_w;
    if (alfa >= 256) for (int i = x0; i < x1; i++) p[i] = c;
    else for (int i = x0; i < x1; i++) p[i] = fb_mieszaj(p[i], c, alfa);
}

void fb_zaokr(int x, int y, int w, int h, int r, u32 c, int alfa) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (x >= kx1 || y >= ky1 || x + w <= kx0 || y + h <= ky0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < ky0 || yy >= ky1) continue;
        if (j >= r && j < h - r) { wiersz_alfa(x, x + w, yy, c, alfa); continue; }
        int cy16 = (j < r ? y + r : y + h - r) * 16;
        for (int i = 0; i < r; i++) {
            int a1 = pokrycie(x + i, yy, (x + r) * 16, cy16, r * 16);
            int a2 = pokrycie(x + w - 1 - i, yy, (x + w - r) * 16, cy16, r * 16);
            if (w_klipie(x + i, yy)) mieszaj_px(bb + yy * fb_w + x + i, c, a1 * alfa >> 8);
            if (w_klipie(x + w - 1 - i, yy)) mieszaj_px(bb + yy * fb_w + x + w - 1 - i, c, a2 * alfa >> 8);
        }
        wiersz_alfa(x + r, x + w - r, yy, c, alfa);
    }
}

void fb_zaokr_gradient(int x, int y, int w, int h, int r, u32 c1, u32 c2) {
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    for (int j = 0; j < h; j++) {
        fb_klip_zawez(x, y + j, w, 1);
        fb_zaokr(x, y, w, h, r, fb_mieszaj(c1, c2, h > 1 ? j * 256 / (h - 1) : 0), 256);
        fb_klip(kx, ky, kw, kh);
    }
}

void fb_zaokr_ramka(int x, int y, int w, int h, int r, u32 c, int alfa) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    wiersz_alfa(x + r, x + w - r, y, c, alfa);
    wiersz_alfa(x + r, x + w - r, y + h - 1, c, alfa);
    for (int j = y + r; j < y + h - r; j++) {
        if (w_klipie(x, j)) mieszaj_px(bb + j * fb_w + x, c, alfa);
        if (w_klipie(x + w - 1, j)) mieszaj_px(bb + j * fb_w + x + w - 1, c, alfa);
    }
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            int a = pokrycie(x + i, y + j, (x + r) * 16, (y + r) * 16, r * 16) - pokrycie(x + i, y + j, (x + r) * 16, (y + r) * 16, r * 16 - 16);
            if (a <= 0) continue;
            a = a * alfa >> 8;
            int px[4] = { x + i, x + w - 1 - i, x + i, x + w - 1 - i }, py[4] = { y + j, y + j, y + h - 1 - j, y + h - 1 - j };
            for (int k = 0; k < 4; k++) if (w_klipie(px[k], py[k])) mieszaj_px(bb + py[k] * fb_w + px[k], c, a);
        }
}

/* ---------- koła i linie ---------- */

void fb_kolo_aa(int cx, int cy, int r, u32 c, int alfa) {
    int r16 = r * 16, cx16 = cx * 16, cy16 = cy * 16;
    for (int y = cy - r - 1; y <= cy + r; y++) {
        if (y < ky0 || y >= ky1) continue;
        int dy = y * 16 + 8 - cy16;
        int wew = (r16 - 16) * (r16 - 16) - dy * dy;
        int pol = wew > 0 ? (int)pierw((u32)wew) / 16 : 0;    /* piksele na pewno w środku */
        int xa = pol > 0 ? cx - pol : 0x7FFFFFF, xb = cx + pol - 1;
        for (int x = cx - r - 1; x <= cx + r; x++) {
            if (x == xa) { wiersz_alfa(xa, xb + 1, y, c, alfa); x = xb; continue; }
            if (!w_klipie(x, y)) continue;
            int a = pokrycie(x, y, cx16, cy16, r16);
            if (a) mieszaj_px(bb + y * fb_w + x, c, a * alfa >> 8);
        }
    }
}

void fb_okrag_aa(int cx, int cy, int r, int grub, u32 c, int alfa) {
    int r16 = r * 16, rw = (r - grub) * 16;
    int ya = cy - r - 1 < ky0 ? ky0 : cy - r - 1, yb = cy + r >= ky1 ? ky1 - 1 : cy + r;
    int xa = cx - r - 1 < kx0 ? kx0 : cx - r - 1, xb = cx + r >= kx1 ? kx1 - 1 : cx + r;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            if (!w_klipie(x, y)) continue;
            int dx = x * 16 + 8 - cx * 16, dy = y * 16 + 8 - cy * 16, d2 = dx * dx + dy * dy;
            if (rw > 16 && d2 < (rw - 16) * (rw - 16)) continue;
            int a = pokrycie(x, y, cx * 16, cy * 16, r16) - (rw > 0 ? pokrycie(x, y, cx * 16, cy * 16, rw) : 0);
            if (a > 0) mieszaj_px(bb + y * fb_w + x, c, a * alfa >> 8);
        }
}

void fb_linia_aa(int x0, int y0, int x1, int y1, int grub, u32 c) {
    long long vx = (x1 - x0) * 16, vy = (y1 - y0) * 16;
    long long dl2 = vx * vx + vy * vy;
    int dl = (int)pierw((u32)dl2);
    int pol16 = grub * 8;
    int m = grub / 2 + 2;
    int xa = (x0 < x1 ? x0 : x1) - m, xb = (x0 > x1 ? x0 : x1) + m;
    int ya = (y0 < y1 ? y0 : y1) - m, yb = (y0 > y1 ? y0 : y1) + m;
    if (xa < kx0) xa = kx0;
    if (xb >= kx1) xb = kx1 - 1;
    if (ya < ky0) ya = ky0;
    if (yb >= ky1) yb = ky1 - 1;
    for (int y = ya; y <= yb; y++)
        for (int x = xa; x <= xb; x++) {
            if (!w_klipie(x, y)) continue;
            long long wx = x * 16 + 8 - x0 * 16, wy = y * 16 + 8 - y0 * 16;
            long long t = wx * vx + wy * vy;
            int d;
            if (dl == 0 || t <= 0) d = (int)pierw((u32)(wx * wx + wy * wy));
            else if (t >= dl2) { long long ex = x * 16 + 8 - x1 * 16, ey = y * 16 + 8 - y1 * 16; d = (int)pierw((u32)(ex * ex + ey * ey)); }
            else { long long kr = wx * vy - wy * vx; if (kr < 0) kr = -kr; d = (int)(kr / dl); }
            int a = pol16 - d + 8;
            if (a <= 0) continue;
            mieszaj_px(bb + y * fb_w + x, c, a >= 16 ? 256 : a * 16);
        }
}

/* ---------- cień pod oknem: miękki, zanikający ---------- */

void fb_cien(int x, int y, int w, int h, int r, int S, int A, int oy) {
    int sy = y + oy;
    int x0 = x - S, x1 = x + w + S, y0 = sy - S, y1 = sy + h + S;
    if (x0 < kx0) x0 = kx0;
    if (y0 < ky0) y0 = ky0;
    if (x1 > kx1) x1 = kx1;
    if (y1 > ky1) y1 = ky1;
    int ix0 = x + r, ix1 = x + w - r, iy0 = sy + r, iy1 = sy + h - r;    /* środek bez zaokrągleń */
    for (int j = y0; j < y1; j++) {
        u32 *p = bb + j * fb_w;
        int dy = j < iy0 ? iy0 - j : (j >= iy1 ? j - iy1 + 1 : 0);
        int pod_oknem = j >= y + r && j < y + h - r;
        for (int i = x0; i < x1; i++) {
            if (pod_oknem && i >= x && i < x + w) { i = x + w - 1; continue; }
            int dx = i < ix0 ? ix0 - i : (i >= ix1 ? i - ix1 + 1 : 0);
            int d = (dx && dy) ? (int)pierw((u32)(dx * dx + dy * dy)) : dx + dy;
            d -= r;
            if (d < 0) d = 0;
            if (d >= S) continue;
            int a = A * (S - d) * (S - d) / (S * S);
            u32 c = p[i];
            p[i] = ((((c >> 16) & 0xFF) * (u32)(256 - a) >> 8) << 16) | ((((c >> 8) & 0xFF) * (u32)(256 - a) >> 8) << 8) |
                   ((c & 0xFF) * (u32)(256 - a) >> 8);
        }
    }
}

/* ---------- rogi okien: zapamiętujemy tło pod rogami, a po narysowaniu okna mieszamy je z krawędzią ---------- */

#define ROG_MAX 16
static u32 rogi[4][ROG_MAX * ROG_MAX];

void fb_rogi_zapisz(int x, int y, int w, int h, int r) {
    if (r > ROG_MAX) r = ROG_MAX;
    int ox[4] = { x, x + w - r, x, x + w - r }, oy[4] = { y, y, y + h - r, y + h - r };
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < r; j++)
            for (int i = 0; i < r; i++) {
                int px = ox[k] + i, py = oy[k] + j;
                rogi[k][j * r + i] = (px >= 0 && px < fb_w && py >= 0 && py < fb_h) ? bb[py * fb_w + px] : 0;
            }
}

void fb_rogi_przywroc(int x, int y, int w, int h, int r, u32 ramka) {
    if (r > ROG_MAX) r = ROG_MAX;
    int ox[4] = { x, x + w - r, x, x + w - r }, oy[4] = { y, y, y + h - r, y + h - r };
    int sx[4] = { x + r, x + w - r, x + r, x + w - r }, sy[4] = { y + r, y + r, y + h - r, y + h - r };
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < r; j++)
            for (int i = 0; i < r; i++) {
                int px = ox[k] + i, py = oy[k] + j;
                if (!w_klipie(px, py)) continue;
                int a = pokrycie(px, py, sx[k] * 16, sy[k] * 16, r * 16);
                u32 *p = bb + py * fb_w + px;
                if (ramka != FB_PRZEZR) {
                    int pr = a - pokrycie(px, py, sx[k] * 16, sy[k] * 16, r * 16 - 16);
                    if (pr > 0) *p = fb_mieszaj(*p, ramka, pr);
                }
                *p = fb_mieszaj(rogi[k][j * r + i], *p, a);
            }
}

/* ---------- obrazy z kanałem alfa ---------- */

void fb_obraz_alfa(int x, int y, int w, int h, const u32 *argb, int alfa) {
    int x0 = x < kx0 ? kx0 : x, x1 = x + w > kx1 ? kx1 : x + w;
    int y0 = y < ky0 ? ky0 : y, y1 = y + h > ky1 ? ky1 : y + h;
    for (int j = y0; j < y1; j++) {
        const u32 *z = argb + (j - y) * w + (x0 - x);
        u32 *d = bb + j * fb_w + x0;
        for (int i = x0; i < x1; i++, z++, d++) {
            int a = (int)(*z >> 24);
            if (!a) continue;
            a = (a + (a >> 7)) * alfa >> 8;
            mieszaj_px(d, *z & 0xFFFFFF, a);
        }
    }
}

void fb_ikona_rys(int x, int y, int nr, int rozm) {
    if (nr < 0 || nr >= IKON_N) return;
    const u32 *t = rozm >= 48 ? ikony48 : rozm >= 32 ? ikony32 : ikony24;
    int r = rozm >= 48 ? 48 : rozm >= 32 ? 32 : 24;
    fb_obraz_alfa(x, y, r, r, t + nr * r * r, 256);
}

void fb_kursor(int x, int y) { fb_obraz_alfa(x - 1, y - 1, KURSOR_W, KURSOR_H, kursor_px, 256); }

/* ---------- czcionki wygładzane ---------- */

static const wglif_t *glif_znajdz(const wczcionka_t *f, u32 cp) {
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->g[m].cp == cp) return &f->g[m];
        if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

static const wglif_t *glif(const wczcionka_t *f, u32 cp) {
    const wglif_t *g = glif_znajdz(f, cp);
    if (g) return g;
    if (cp == 0xA0 || cp == '\t') return glif_znajdz(f, ' ');
    if (cp == 0x2011 || cp == 0x2010) return glif_znajdz(f, '-');
    if (cp == 0x2019 || cp == 0x2018) return glif_znajdz(f, '\'');
    if (cp == 0x201E || cp == 0x201C || cp == 0x201D) return glif_znajdz(f, '"');
    if (cp == 0x2026) return glif_znajdz(f, '.');
    return glif_znajdz(f, '?');
}

static void glif_rysuj(int x, int y, const wczcionka_t *f, const wglif_t *g, u32 kolor) {
    int gx = x + g->xo, gy = y + g->yo;
    int i0 = gx < kx0 ? kx0 - gx : 0, j0 = gy < ky0 ? ky0 - gy : 0;
    int i1 = gx + g->w > kx1 ? kx1 - gx : g->w, j1 = gy + g->h > ky1 ? ky1 - gy : g->h;
    const u8 *a = f->a + g->off;
    for (int j = j0; j < j1; j++) {
        u32 *p = bb + (gy + j) * fb_w + gx;
        const u8 *aw = a + j * g->w;
        for (int i = i0; i < i1; i++) {
            int al = aw[i];
            if (!al) continue;
            if (al == 255) p[i] = kolor;
            else p[i] = fb_mieszaj(p[i], kolor, al + (al >> 7));
        }
    }
}

int fb_napis_n(int x, int y, const char *s, int bajty, u32 kolor, int fi) {
    const wczcionka_t *f = &wczcionki[fi];
    int x0 = x;
    int widac = y < ky1 && y + f->wys > ky0;
    while (bajty > 0 && *s) {
        const char *q = s;
        u32 cp = k_utf8_next(&s);
        bajty -= (int)(s - q);
        const wglif_t *g = glif(f, cp);
        if (!g) continue;
        if (widac && g->w && x + g->xo < kx1 && x + g->xo + g->w > kx0) glif_rysuj(x, y, f, g, kolor);
        x += g->adv;
    }
    return x - x0;
}

int fb_napis(int x, int y, const char *s, u32 kolor, int f) { return fb_napis_n(x, y, s, 0x7FFFFFFF, kolor, f); }

int fb_napis_szer_n(const char *s, int bajty, int fi) {
    const wczcionka_t *f = &wczcionki[fi];
    int w = 0;
    while (bajty > 0 && *s) {
        const char *q = s;
        const wglif_t *g = glif(f, k_utf8_next(&s));
        bajty -= (int)(s - q);
        if (g) w += g->adv;
    }
    return w;
}
int fb_napis_szer(const char *s, int f) { return fb_napis_szer_n(s, 0x7FFFFFFF, f); }
int fb_znak_szer(u32 cp, int fi) { const wglif_t *g = glif(&wczcionki[fi], cp); return g ? g->adv : 0; }
int fb_napis_wys(int f) { return wczcionki[f].wys; }
int fb_napis_asc(int f) { return wczcionki[f].asc; }

void fb_napis_srodek(int x, int w, int y, const char *s, u32 kolor, int f) {
    fb_napis(x + (w - fb_napis_szer(s, f)) / 2, y, s, kolor, f);
}

int fb_napis_zawin(int x, int y, int w, const char *s, u32 kolor, int f, int odstep) {
    int linie = 0;
    while (*s) {
        const char *p = s, *ost = 0;
        int sz = 0;
        while (*p && *p != '\n') {
            const char *q = p;
            u32 cp = k_utf8_next(&q);
            int zw = fb_znak_szer(cp, f);
            if (sz + zw > w && p > s) break;
            if (cp == ' ') ost = p;
            sz += zw; p = q;
        }
        const char *koniec = p;
        if (*p && *p != '\n' && ost) koniec = ost;
        if (kolor != FB_PRZEZR) fb_napis_n(x, y + linie * odstep, s, (int)(koniec - s), kolor, f);
        linie++;
        s = koniec;
        if (*s == '\n') s++;
        else while (*s == ' ') s++;
    }
    return linie;
}

void fb_napis_przytnij(char *o, u32 rozm, const char *s, int maxw, int f) {
    if (fb_napis_szer(s, f) <= maxw) { k_strncpy(o, s, rozm); return; }
    int kropki = fb_napis_szer("…", f), sz = 0;
    const char *p = s;
    while (*p) {
        const char *q = p;
        int zw = fb_znak_szer(k_utf8_next(&q), f);
        if (sz + zw + kropki > maxw) break;
        sz += zw; p = q;
    }
    u32 n = (u32)(p - s);
    if (n + 4 > rozm) n = rozm - 4;
    memcpy(o, s, n);
    memcpy(o + n, "…", 4);
}

/* znak terminala 10x20 (kody VGA jak w buforze konsoli) */
void fb_term_znak(int x, int y, u8 g, u32 kolor, u32 tlo) {
    if (x >= kx1 || y >= ky1 || x + TERM_ZW <= kx0 || y + TERM_ZH <= ky0) return;
    const u8 *a = terminal_czc + g * TERM_ZW * TERM_ZH;
    for (int j = 0; j < TERM_ZH; j++) {
        int yy = y + j;
        if (yy < ky0 || yy >= ky1) continue;
        u32 *p = bb + yy * fb_w;
        for (int i = 0; i < TERM_ZW; i++) {
            int xx = x + i;
            if (xx < kx0 || xx >= kx1) continue;
            int al = a[j * TERM_ZW + i];
            p[xx] = al ? (al == 255 ? kolor : fb_mieszaj(tlo, kolor, al + (al >> 7))) : tlo;
        }
    }
}

/* ---------- tapeta: powiększenie dwuliniowe z ditheringiem + ostry krzyż ---------- */

static u32 *tapeta;

void fb_tapeta_init(void) {
    tapeta = pamiec_duza((u32)fb_w * (u32)fb_h * 4u);
    if (!tapeta) return;
    static const u8 bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    for (int y = 0; y < fb_h; y++) {
        int sy = (y * 2 + 1) * TAPETA_H * 128 / fb_h - 128;      /* środek piksela, 1/256 */
        if (sy < 0) sy = 0;
        int y0 = sy >> 8, fy = sy & 255, y1 = y0 + 1 < TAPETA_H ? y0 + 1 : y0;
        for (int x = 0; x < fb_w; x++) {
            int sx = (x * 2 + 1) * TAPETA_W * 128 / fb_w - 128;
            if (sx < 0) sx = 0;
            int x0 = sx >> 8, fx = sx & 255, x1 = x0 + 1 < TAPETA_W ? x0 + 1 : x0;
            const u8 *a = tapeta_rgb + (y0 * TAPETA_W + x0) * 3, *b = tapeta_rgb + (y0 * TAPETA_W + x1) * 3;
            const u8 *c = tapeta_rgb + (y1 * TAPETA_W + x0) * 3, *d = tapeta_rgb + (y1 * TAPETA_W + x1) * 3;
            int prog = bayer[(y & 3) * 4 + (x & 3)] * 16 * 256;
            u32 px = 0;
            for (int k = 0; k < 3; k++) {
                int g = (a[k] * (256 - fx) + b[k] * fx) * (256 - fy) + (c[k] * (256 - fx) + d[k] * fx) * fy;
                int v = (g + prog) >> 16;
                if (v > 255) v = 255;
                px = (px << 8) | (u32)v;
            }
            tapeta[y * fb_w + x] = px;
        }
    }
    /* krzyż rysujemy w pełnej rozdzielczości na tapecie */
    u32 *stary = bb;
    bb = tapeta;
    fb_klip_caly();
    int cx = fb_w * 330 / 480, gy = fb_h * 52 / 270, dy = fb_h * 150 / 270, ry = fb_h * 82 / 270;
    int sz = fb_w / 240 > 3 ? fb_w / 240 : 3, ram = fb_w * 30 / 480;
    fb_zaokr(cx - sz / 2, gy, sz, dy - gy, sz / 2, 0xFFF4D6, 100);
    fb_zaokr(cx - ram, ry - sz / 2, 2 * ram, sz, sz / 2, 0xFFF4D6, 100);
    bb = stary;
}

void fb_tapeta(int x, int y, int w, int h) {
    int x0 = x < kx0 ? kx0 : x, x1 = x + w > kx1 ? kx1 : x + w;
    int y0 = y < ky0 ? ky0 : y, y1 = y + h > ky1 ? ky1 : y + h;
    if (x1 <= x0) return;
    if (!tapeta) { for (int j = y0; j < y1; j++) fb_prost(x0, j, x1 - x0, 1, fb_mieszaj(0x16224A, 0x1A0F33, j * 256 / fb_h)); return; }
    for (int j = y0; j < y1; j++) memcpy(bb + j * fb_w + x0, tapeta + j * fb_w + x0, (u32)(x1 - x0) * 4u);
}
