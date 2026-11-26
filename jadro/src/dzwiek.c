/* AmenOS - czas (PIT, kanał 0, odpytywany) i głośniczek PC (PIT, kanał 2) */
#include "kernel.h"

#define PIT_HZ 1193182u

static u16 ostatni;
static u32 ms_razem, reszta;

static u16 pit_odczyt(void) {
    outb(0x43, 0x00);                    /* zatrzaśnij licznik kanału 0 */
    u8 lo = inb(0x40), hi = inb(0x40);
    return (u16)(lo | (hi << 8));
}

void czasomierz_init(void) {
    outb(0x43, 0x34);                    /* kanał 0, tryb 2, lo/hi */
    outb(0x40, 0x00);                    /* dzielnik 65536 - pełny okres ok. 55 ms */
    outb(0x40, 0x00);
    ostatni = pit_odczyt();
}

/* milisekundy od startu; wystarczy wołać częściej niż co 50 ms */
u32 zegar_ms(void) {
    u16 teraz = pit_odczyt();
    reszta += (u16)(ostatni - teraz);
    ostatni = teraz;
    ms_razem += reszta / 1193;
    reszta %= 1193;
    return ms_razem;
}

void czekaj_ms(u32 ms) {
    u32 t0 = zegar_ms();
    while (zegar_ms() - t0 < ms) { audio_pompuj(); if (gui_aktywne) gui_pompuj(); }
}

/* Z kartą dźwiękową "głośniczek" gra organami - wszystkie stare miejsca brzmią od razu lepiej. */
static u16 glosnik_glos;

void glosnik_ton(u32 hz) {
    if (audio_jest) {
        if (glosnik_glos) dzw_pusc(glosnik_glos);
        glosnik_glos = hz >= 20 ? dzw_nuta(INS_ORGANY, hz * 1000, 0, 150) : 0;
        return;
    }
    if (hz < 20) { glosnik_cisza(); return; }
    u32 dz = PIT_HZ / hz;
    outb(0x43, 0xB6);                    /* kanał 2, fala prostokątna */
    outb(0x42, (u8)(dz & 0xFF));
    outb(0x42, (u8)(dz >> 8));
    outb(0x61, inb(0x61) | 0x03);
}

void glosnik_cisza(void) {
    if (glosnik_glos) { dzw_pusc(glosnik_glos); glosnik_glos = 0; }
    outb(0x61, inb(0x61) & 0xFC);
}
