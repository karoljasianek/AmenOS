/* AmenOS - ekran tekstowy 80x25 z polskimi znakami.
 * W trybie tekstowym pisze do pamięci karty VGA, w graficznym - do bufora rysowanego w oknie terminala. */
#include "kernel.h"
#include "font_pl.h"
#include "znaki_dane.h"

static volatile u16 *VGA = (volatile u16 *)0xB8000;
static u16 bufor_graficzny[VGA_W * VGA_H];
static int grafika, kursor_wl = 1;
static int cx, cy;
static u8 attr = ATTR(SZARY, CZARNY);

/* Wgrywa polskie litery do pamięci czcionki karty VGA (płaszczyzna 2). */
static void wgraj_polskie_litery(void) {
    volatile u8 *font = (volatile u8 *)0xA0000;
    outw(0x3C4, 0x0402);   /* zapis tylko do płaszczyzny 2 */
    outw(0x3C4, 0x0704);   /* dostęp sekwencyjny */
    outw(0x3CE, 0x0204);   /* odczyt z płaszczyzny 2 */
    outw(0x3CE, 0x0005);   /* bez trybu parzyste/nieparzyste */
    outw(0x3CE, 0x0406);   /* pamięć VGA pod 0xA0000 */
    for (int i = 0; i < PL_COUNT; i++)
        for (int r = 0; r < 16; r++)
            font[(PL_SLOT0 + i) * 32 + r] = pl_glyphs[i][r];
    outw(0x3C4, 0x0302);   /* przywrócenie trybu tekstowego */
    outw(0x3C4, 0x0304);
    outw(0x3CE, 0x0004);
    outw(0x3CE, 0x1005);
    outw(0x3CE, 0x0E06);
}

/* Wyłącza miganie, żeby mieć 16 kolorów tła (biały, różowy, żółty...). */
static void wylacz_miganie(void) {
    inb(0x3DA);
    outb(0x3C0, 0x30);
    u8 v = inb(0x3C1);
    inb(0x3DA);
    outb(0x3C0, 0x30);
    outb(0x3C0, v & ~0x08);
    inb(0x3DA);
    outb(0x3C0, 0x20);     /* włącz z powrotem wyświetlanie */
}

static void kursor_sprzet(void) {
    if (grafika) return;
    u16 pos = (u16)(cy * VGA_W + cx);
    outb(0x3D4, 0x0F); outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (u8)(pos >> 8));
}

void vga_cursor(int on) {
    kursor_wl = on;
    if (grafika) return;
    if (on) {
        outb(0x3D4, 0x0A); outb(0x3D5, (inb(0x3D5) & 0xC0) | 13);
        outb(0x3D4, 0x0B); outb(0x3D5, (inb(0x3D5) & 0xE0) | 14);
    } else {
        outb(0x3D4, 0x0A); outb(0x3D5, 0x20);
    }
}

u16 *vga_bufor(void) { return (u16 *)VGA; }
int vga_kursor_widoczny(void) { return kursor_wl; }

void vga_init(int tryb_graficzny) {
    grafika = tryb_graficzny;
    if (grafika) {
        VGA = bufor_graficzny;
        vga_clear();
        return;
    }
    wgraj_polskie_litery();
    wylacz_miganie();
    vga_cursor(1);
    vga_clear();
}

void vga_color(u8 fg, u8 bg) { attr = ATTR(fg, bg); }
u8 vga_attr(void) { return attr; }
int vga_x(void) { return cx; }
int vga_y(void) { return cy; }

void vga_clear(void) {
    for (int i = 0; i < VGA_W * VGA_H; i++) VGA[i] = (u16)(attr << 8) | ' ';
    cx = cy = 0;
    kursor_sprzet();
}

void vga_goto(int x, int y) { cx = x; cy = y; kursor_sprzet(); }

static void przewin(void) {
    for (int i = 0; i < VGA_W * (VGA_H - 1); i++) VGA[i] = VGA[i + VGA_W];
    for (int i = 0; i < VGA_W; i++) VGA[VGA_W * (VGA_H - 1) + i] = (u16)(attr << 8) | ' ';
    cy = VGA_H - 1;
}

/* Zamienia znak Unicode na numer znaku w czcionce VGA (CP437 + polskie). */
u8 vga_map(u32 c) {
    if (c >= 0x20 && c < 0x7F) return (u8)c;
    for (int i = 0; i < PL_COUNT; i++)
        if (pl_codepoints[i] == c) return (u8)(PL_SLOT0 + i);
    switch (c) {
    case 0x2588: return 0xDB;  /* █ */
    case 0x2580: return 0xDF;  /* ▀ */
    case 0x2584: return 0xDC;  /* ▄ */
    case 0x2591: return 0xB0;  /* ░ */
    case 0x2592: return 0xB1;  /* ▒ */
    case 0x2500: return 0xC4;  /* ─ */
    case 0x2502: return 0xB3;  /* │ */
    case 0x250C: return 0xDA;  /* ┌ */
    case 0x2510: return 0xBF;  /* ┐ */
    case 0x2514: return 0xC0;  /* └ */
    case 0x2518: return 0xD9;  /* ┘ */
    case 0x253C: return 0xC5;  /* ┼ */
    case 0x2550: return 0xCD;  /* ═ */
    case 0x2551: return 0xBA;  /* ║ */
    case 0x2554: return 0xC9;  /* ╔ */
    case 0x2557: return 0xBB;  /* ╗ */
    case 0x255A: return 0xC8;  /* ╚ */
    case 0x255D: return 0xBC;  /* ╝ */
    case 0x2022: return 0x07;  /* • */
    case 0x00B7: return 0xFA;  /* · */
    case 0x2192: return 0x1A;  /* → */
    case 0x2190: return 0x1B;  /* ← */
    case 0x25BA: case 0x25B6: return 0x10;  /* ► */
    case 0x25C4: case 0x25C0: return 0x11;  /* ◄ */
    case 0x25A0: return 0xFE;  /* ■ */
    case 0x2191: return 0x18;  /* ↑ */
    case 0x2193: return 0x19;  /* ↓ */
    case 0x266A: return 0x0D;  /* ♪ */
    case 0x266B: return 0x0E;  /* ♫ */
    case 0x00FC: return 0x81;  /* ü */
    case 0x00E9: return 0x82;  /* é */
    case 0x00E4: return 0x84;  /* ä */
    case 0x00F6: return 0x94;  /* ö */
    case 0x00DF: return 0xE1;  /* ß */
    case 0x2014: case 0x2013: return '-';
    case 0x201E: case 0x201D: case 0x201C: return '"';
    case 0x00B0: return 0xF8;  /* ° */
    case 0x2026: return '.';   /* … */
    case 0x2018: case 0x2019: case 0x201A: return '\'';
    case 0x20AC: return 'E';   /* € */
    }
    if (c >= 0xA0 && c < 0x180) return lacinskie_vga[c - 0xA0];
    return '?';
}

void vga_glyph_at(int x, int y, u8 g, u8 a) {
    if (x < 0 || y < 0 || x >= VGA_W || y >= VGA_H) return;
    VGA[y * VGA_W + x] = (u16)(a << 8) | g;
}

void vga_fill(int x, int y, int w, int h, u8 g, u8 a) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) vga_glyph_at(x + i, y + j, g, a);
}

void vga_puts_at(int x, int y, const char *s, u8 a) {
    while (*s) {
        u32 c = k_utf8_next(&s);
        vga_glyph_at(x++, y, vga_map(c), a);
    }
}

void vga_putc(u32 c) {
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\r') { cx = 0; }
    else if (c == '\t') { cx = (cx + 4) & ~3; }
    else {
        VGA[cy * VGA_W + cx] = (u16)(attr << 8) | vga_map(c);
        cx++;
    }
    if (cx >= VGA_W) { cx = 0; cy++; }
    if (cy >= VGA_H) przewin();
    kursor_sprzet();
}

void vga_backspace(void) {
    if (cx > 0) cx--;
    else if (cy > 0) { cy--; cx = VGA_W - 1; }
    VGA[cy * VGA_W + cx] = (u16)(attr << 8) | ' ';
    kursor_sprzet();
}

void vga_puts(const char *s) {
    while (*s) vga_putc(k_utf8_next(&s));
}

/* ---------- prosty printf: %s %d %u %x %c, szerokość, '0', '-' ---------- */

typedef struct { char *buf; u32 max, len; } sink_t;

static void emit(sink_t *k, const char *s, u32 n) {
    if (!k) { for (u32 i = 0; i < n; i++) vga_putc((u8)s[i] < 0x80 ? (u8)s[i] : 0); return; }
    for (u32 i = 0; i < n && k->len + 1 < k->max; i++) k->buf[k->len++] = s[i];
}

static void emit_utf8(sink_t *k, const char *s) {
    if (!k) { vga_puts(s); return; }
    emit(k, s, k_strlen(s));
}

static void pad(sink_t *k, int n, char ch) { while (n-- > 0) emit(k, &ch, 1); }

static void vformat(sink_t *k, const char *f, __builtin_va_list ap) {
    while (*f) {
        if (*f != '%') {
            const char *start = f;
            while (*f && *f != '%') f++;
            if (!k) { for (const char *p = start; p < f;) vga_putc(k_utf8_next(&p)); }
            else emit(k, start, (u32)(f - start));
            continue;
        }
        f++;
        int left = 0, zero = 0, width = 0;
        if (*f == '-') { left = 1; f++; }
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        char tmp[16]; int n = 0;
        switch (*f) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)k_utf8_len(s);
            if (!left) pad(k, width - len, ' ');
            emit_utf8(k, s);
            if (left) pad(k, width - len, ' ');
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            emit(k, &c, 1);
            break;
        }
        case 'd': case 'u': case 'x': {
            int v = __builtin_va_arg(ap, int);
            u32 u = (u32)v; int neg = 0;
            u32 base = (*f == 'x') ? 16 : 10;
            if (*f == 'd' && v < 0) { neg = 1; u = (u32)(-v); }
            do { tmp[n++] = "0123456789abcdef"[u % base]; u /= base; } while (u);
            if (neg) tmp[n++] = '-';
            int len = n;
            if (!left) pad(k, width - len, zero ? '0' : ' ');
            while (n) emit(k, &tmp[--n], 1);
            if (left) pad(k, width - len, ' ');
            break;
        }
        case '%': emit(k, "%", 1); break;
        default: break;
        }
        if (*f) f++;
    }
}

void kprintf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vformat(0, fmt, ap);
    __builtin_va_end(ap);
}

void ksprintf(char *out, u32 max, const char *fmt, ...) {
    sink_t k = { out, max, 0 };
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vformat(&k, fmt, ap);
    __builtin_va_end(ap);
    if (max) out[k.len < max ? k.len : max - 1] = 0;
}
