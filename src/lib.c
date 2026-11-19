/* AmenOS - drobne funkcje biblioteczne (bez libc) */
#include "kernel.h"

void *memset(void *dst, int c, u32 n) {
    u8 *d = dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, u32 n) {
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

u32 k_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

int k_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}

void k_strncpy(char *dst, const char *src, u32 max) {
    u32 i = 0;
    if (!max) return;
    while (src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    /* nie ucinaj znaku UTF-8 w połowie */
    if (i > 0 && ((u8)dst[i - 1] & 0x80)) {
        u32 j = i - 1;
        while (j > 0 && ((u8)dst[j] & 0xC0) == 0x80) j--;
        u8 lead = (u8)dst[j];
        u32 need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
        if (i - j < need) i = j;
    }
    dst[i] = 0;
}

int k_atoi(const char *s, int *ok) {
    int v = 0, neg = 0, any = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (ok) *ok = any && *s == 0;
    return neg ? -v : v;
}

char *k_token(char **p) {
    char *s = *p;
    while (*s == ' ') s++;
    if (!*s) { *p = s; return 0; }
    char *start = s;
    while (*s && *s != ' ') s++;
    if (*s) *s++ = 0;
    *p = s;
    return start;
}

u32 k_utf8_next(const char **ps) {
    const u8 *s = (const u8 *)*ps;
    u32 c = *s;
    if (c < 0x80) { *ps += 1; return c; }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *ps += 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *ps += 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    *ps += 1;
    return '?';
}

u32 k_utf8_len(const char *s) {
    u32 n = 0;
    while (*s) { k_utf8_next(&s); n++; }
    return n;
}

static char fold_char(u32 c) {
    switch (c) {
    case 0x105: case 0x104: return 'a';
    case 0x107: case 0x106: return 'c';
    case 0x119: case 0x118: return 'e';
    case 0x142: case 0x141: return 'l';
    case 0x144: case 0x143: return 'n';
    case 0x0F3: case 0x0D3: return 'o';
    case 0x15B: case 0x15A: return 's';
    case 0x17A: case 0x179: case 0x17C: case 0x17B: return 'z';
    }
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    if (c < 0x80) return (char)c;
    return '?';
}

void k_fold_pl(char *dst, const char *src, u32 max) {
    u32 i = 0;
    while (*src && i + 1 < max) dst[i++] = fold_char(k_utf8_next(&src));
    dst[i] = 0;
}

int k_utf8_put(u32 c, char *o) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

int k_memcmp(const void *a, const void *b, u32 n) {
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
