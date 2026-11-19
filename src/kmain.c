/* AmenOS - główna funkcja jądra */
#include "kernel.h"

void kmain(u32 magic, const u32 *mbi) {
    vga_init();
    czasomierz_init();
    u32 pamiec_kb = 0;
    if (magic == 0x2BADB002 && (mbi[0] & 1))
        pamiec_kb = mbi[2] + 1024;          /* pamięć powyżej 1 MB + pierwszy megabajt */
    ekran_powitalny();
    powloka(pamiec_kb);
}
