/* AmenOS - obsługa klawiatury PS/2 (układ polski programisty, AltGr + litera) */
#include "kernel.h"

static const char mapa[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']', '\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};
static const char mapa_shift[] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}', '\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

static int shift, capslock, altgr, e0;
int kbd_historia = 1;   /* 0 = tryb prywatny: bez historii, F10 przerywa */

static int polska_litera(char c, int duza) {
    switch (c) {
    case 'a': return duza ? 0x104 : 0x105;
    case 'c': return duza ? 0x106 : 0x107;
    case 'e': return duza ? 0x118 : 0x119;
    case 'l': return duza ? 0x141 : 0x142;
    case 'n': return duza ? 0x143 : 0x144;
    case 'o': return duza ? 0x0D3 : 0x0F3;
    case 's': return duza ? 0x15A : 0x15B;
    case 'x': return duza ? 0x179 : 0x17A;
    case 'z': return duza ? 0x17B : 0x17C;
    }
    return 0;
}

int kbd_poll(void) {
    u8 st = inb(0x64);
    if (!(st & 1)) return -1;
    u8 sc = inb(0x60);
    if (st & 0x20) return -1;               /* dane z myszy - pomijamy */

    if (sc == 0xE0) { e0 = 1; return -1; }
    if (e0) {
        e0 = 0;
        switch (sc) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x1C: return KEY_ENTER;
        case 0x38: altgr = 1; return -1;
        case 0xB8: altgr = 0; return -1;
        }
        return -1;
    }
    switch (sc) {
    case 0x2A: case 0x36: shift = 1; return -1;
    case 0xAA: case 0xB6: shift = 0; return -1;
    case 0x3A: capslock = !capslock; return -1;
    case 0x3B: return KEY_F1;
    case 0x44: return KEY_F10;
    }
    if (sc & 0x80) return -1;               /* puszczenie klawisza */
    if (sc >= sizeof(mapa)) return -1;

    char c = mapa[sc];
    if (!c) return -1;
    int litera = (c >= 'a' && c <= 'z');
    int duza = litera ? (shift ^ capslock) : shift;
    if (altgr && litera) {
        int pl = polska_litera(c, duza);
        if (pl) return pl;
    }
    if (litera) return duza ? c - 32 : c;
    return shift ? mapa_shift[sc] : c;
}

int kbd_getkey(void) {
    int k;
    while ((k = kbd_poll()) < 0) __asm__ volatile("pause");
    return k;
}

/* ---------- edycja linii z historią ---------- */

#define HIST 12
static char historia[HIST][128];
static int hist_n;

static int utf8_encode(u32 c, char *o) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

static void wymaz_linie(char *buf, int *len) {
    int n = (int)k_utf8_len(buf);
    while (n--) vga_backspace();
    *len = 0; buf[0] = 0;
}

int kbd_readline(char *buf, int max) {
    int len = 0, hpos = hist_n;
    buf[0] = 0;
    for (;;) {
        int k = kbd_getkey();
        if (k == KEY_ENTER) {
            vga_putc('\n');
            if (kbd_historia && len && (hist_n == 0 || k_strcmp(historia[(hist_n - 1) % HIST], buf) != 0)) {
                k_strncpy(historia[hist_n % HIST], buf, sizeof historia[0]);
                hist_n++;
            }
            return len;
        }
        if (k == KEY_BKSP) {
            if (len > 0) {
                do { len--; } while (len > 0 && ((u8)buf[len] & 0xC0) == 0x80);
                buf[len] = 0;
                vga_backspace();
            }
            continue;
        }
        if (k == KEY_ESC) { wymaz_linie(buf, &len); continue; }
        if (k == KEY_F10 && !kbd_historia) {
            wymaz_linie(buf, &len);
            memset(buf, 0, (u32)max);
            return -1;
        }
        if ((k == KEY_UP || k == KEY_DOWN || k == KEY_F1) && !kbd_historia) continue;
        if (k == KEY_UP || k == KEY_DOWN) {
            int oldest = hist_n > HIST ? hist_n - HIST : 0;
            if (k == KEY_UP && hpos > oldest) hpos--;
            else if (k == KEY_DOWN && hpos < hist_n) hpos++;
            else continue;
            wymaz_linie(buf, &len);
            if (hpos < hist_n) {
                k_strncpy(buf, historia[hpos % HIST], (u32)max);
                len = (int)k_strlen(buf);
                vga_puts(buf);
            }
            continue;
        }
        if (k == KEY_F1) {
            wymaz_linie(buf, &len);
            k_strncpy(buf, "pomoc", (u32)max);
            len = 5; vga_puts(buf);
            continue;
        }
        if (k < 0x20 || (k >= 0x1000 && k < 0x1100)) continue;
        char enc[4];
        int n = utf8_encode((u32)k, enc);
        if (len + n >= max) continue;
        for (int i = 0; i < n; i++) buf[len++] = enc[i];
        buf[len] = 0;
        vga_putc((u32)k);
    }
}
