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

/* ---------- wspólny port 8042: klawiatura i mysz ---------- */

static u8 kolejka[64];                      /* kody klawiszy czekające na odczyt */
static int kol_od, kol_do;
static u32 wstrzyk[128];                     /* znaki wstrzyknięte do terminala (z pulpitu) */
static int wst_od, wst_do;
static int mysz_jest;

static void ps2_czekaj_zapis(void) { u32 t = zegar_ms(); while ((inb(0x64) & 2) && zegar_ms() - t < 50) ; }
static int ps2_czekaj_odczyt(void) { u32 t = zegar_ms(); while (zegar_ms() - t < 100) if (inb(0x64) & 1) return 1; return 0; }

static void mysz_pisz(u8 b) {
    ps2_czekaj_zapis(); outb(0x64, 0xD4);
    ps2_czekaj_zapis(); outb(0x60, b);
    if (ps2_czekaj_odczyt()) inb(0x60);       /* potwierdzenie 0xFA */
}

void mysz_init(void) {
    if (inb(0x64) == 0xFF) return;            /* brak kontrolera PS/2 - zostaje mysz USB */
    ps2_czekaj_zapis(); outb(0x64, 0xA8);    /* włącz port myszy */
    ps2_czekaj_zapis(); outb(0x64, 0x20);
    u8 cfg = ps2_czekaj_odczyt() ? inb(0x60) : 0x47;
    cfg &= (u8)~0x20;                         /* zegar myszy włączony */
    cfg &= (u8)~0x02;                         /* bez przerwań - system odpytuje */
    ps2_czekaj_zapis(); outb(0x64, 0x60);
    ps2_czekaj_zapis(); outb(0x60, cfg);
    mysz_pisz(0xF6);                          /* ustawienia domyślne */
    mysz_pisz(0xF4);                          /* nadawanie ruchów */
    mysz_jest = 1;
}

static void mysz_bajt(u8 b) {
    static u8 pak[3];
    static int n;
    if (n == 0 && !(b & 0x08)) return;       /* synchronizacja pakietu */
    pak[n++] = b;
    if (n < 3) return;
    n = 0;
    if (pak[0] & 0xC0) return;               /* przepełnienie */
    int dx = (int)pak[1] - ((pak[0] << 4) & 0x100);
    int dy = (int)pak[2] - ((pak[0] << 3) & 0x100);
    if (gui_aktywne) gui_mysz(dx, -dy, pak[0] & 7);
}

void kbd_wloz_kod(u8 kod) {
    if (((kol_do + 1) & 63) != kol_od) { kolejka[kol_do] = kod; kol_do = (kol_do + 1) & 63; }
}

void ps2_obsluz(void) {
    audio_pompuj();
    usb_odpytaj();
    for (int i = 0; i < 32; i++) {
        u8 st = inb(0x64);
        if (st == 0xFF || !(st & 1)) break;       /* 0xFF: komputer bez kontrolera PS/2 */
        u8 b = inb(0x60);
        if (st & 0x20) { if (mysz_jest) mysz_bajt(b); }
        else if (((kol_do + 1) & 63) != kol_od) { kolejka[kol_do] = b; kol_do = (kol_do + 1) & 63; }
    }
}

void kbd_wstrzyknij(const char *s) {
    while (*s && ((wst_do + 1) & 127) != wst_od) { wstrzyk[wst_do] = k_utf8_next(&s); wst_do = (wst_do + 1) & 127; }
}

static int dekoduj(u8 sc);

int kbd_poll(void) {
    if (gui_aktywne) gui_pompuj();
    ps2_obsluz();
    int dla_terminala = !gui_aktywne || gui_terminal_aktywny();
    if (dla_terminala && wst_od != wst_do) {
        u32 c = wstrzyk[wst_od]; wst_od = (wst_od + 1) & 127;
        return (int)c;
    }
    while (kol_od != kol_do) {
        u8 sc = kolejka[kol_od]; kol_od = (kol_od + 1) & 63;
        int k = dekoduj(sc);
        if (k < 0) continue;
        if (!dla_terminala) { gui_klawisz(k); continue; }
        return k;
    }
    return -1;
}

static int dekoduj(u8 sc) {
    if (sc == 0xE0) { e0 = 1; return -1; }
    if (e0) {
        e0 = 0;
        switch (sc) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x49: return KEY_PGUP;
        case 0x51: return KEY_PGDN;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
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
