/* AmenOS - główna funkcja jądra */
#include "kernel.h"

extern char koniec_jadra[];                     /* z linker.ld */

/* duże obszary pamięci za jądrem (bufor ekranu, obrazki); bez zwalniania */
static u32 wolna, granica;
void *pamiec_duza(u32 rozmiar) {
    u32 p = (wolna + 0xFFFu) & ~0xFFFu;
    if (!granica || p + rozmiar > granica) return 0;
    wolna = p + rozmiar;
    return (void *)p;
}

void kmain(u32 magic, const u32 *mbi) {
    czasomierz_init();
    u32 pamiec_kb = 0;
    if (magic == 0x2BADB002 && (mbi[0] & 1))
        pamiec_kb = mbi[2] + 1024;              /* pamięć powyżej 1 MB + pierwszy megabajt */
    /* "bezusb" w linii poleceń (menu GRUB-a) wyłącza sterowniki USB - zostaje emulacja PS/2 z BIOS-u */
    int bez_usb = 0;
    if (magic == 0x2BADB002 && (mbi[0] & 4) && mbi[4]) {
        const char *c = (const char *)mbi[4];
        for (; *c; c++) if (c[0] == 'b' && c[1] == 'e' && c[2] == 'z' && c[3] == 'u' && c[4] == 's' && c[5] == 'b') bez_usb = 1;
    }
    if (!bez_usb) usb_init();
    else usb_wylaczone = 1;
    wolna = (u32)koniec_jadra;
    granica = pamiec_kb ? 0x100000u + pamiec_kb * 1024u - 0x10000u : 0;
    if (granica > 0xE0000000u) granica = 0xE0000000u;
    int grafika = magic == 0x2BADB002 && fb_init(mbi);
    vga_init(grafika);
    if (grafika) {
        gui_init(pamiec_kb);
        gui_powitanie();
    } else {
        ekran_powitalny();
    }
    powloka(pamiec_kb);
}
