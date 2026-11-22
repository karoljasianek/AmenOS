/* AmenOS - główna funkcja jądra */
#include "kernel.h"

extern char koniec_jadra[];                     /* z linker.ld */

void kmain(u32 magic, const u32 *mbi) {
    czasomierz_init();
    u32 pamiec_kb = 0;
    if (magic == 0x2BADB002 && (mbi[0] & 1))
        pamiec_kb = mbi[2] + 1024;              /* pamięć powyżej 1 MB + pierwszy megabajt */
    int grafika = magic == 0x2BADB002 && fb_init(mbi, (u32)koniec_jadra, pamiec_kb);
    vga_init(grafika);
    if (grafika) {
        gui_init(pamiec_kb);
        gui_powitanie();
    } else {
        ekran_powitalny();
    }
    powloka(pamiec_kb);
}
