/* AmenOS - grafika: bufor ramki ustawiony przez GRUB (VBE) i podstawowe rysowanie.
 * Rysujemy do bufora w pamięci RAM, a gotowe fragmenty kopiujemy na ekran. */
#include "kernel.h"
#include "grafika_dane.h"

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
    if (adr_hi || typ != 1 || (fb_bpp != 32 && fb_bpp != 24) || fb_w < 800 || fb_h < 600 || fb_w > 1920 || fb_h > 1200)
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
        if (fb_bpp == 32 && prosty) {
            u32 *d32 = (u32 *)d;
            for (int i = 0; i < w; i++) d32[i] = z[i];
        } else if (fb_bpp == 32) {
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
