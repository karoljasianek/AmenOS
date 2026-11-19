/* AmenOS - zegar czasu rzeczywistego (CMOS RTC) */
#include "kernel.h"

int strefa_godzin = 0;   /* przesunięcie względem zegara sprzętowego */

static u8 cmos(u8 reg) { outb(0x70, reg); io_wait(); return inb(0x71); }
static int aktualizacja(void) { return cmos(0x0A) & 0x80; }
static int bcd(int v) { return (v & 0x0F) + (v >> 4) * 10; }

static void surowy(czas_t *c, int *wiek) {
    while (aktualizacja()) ;
    c->sek = cmos(0x00); c->min = cmos(0x02); c->godz = cmos(0x04);
    c->dzien = cmos(0x07); c->mies = cmos(0x08); c->rok = cmos(0x09);
    *wiek = cmos(0x32);
}

void rtc_czytaj(czas_t *c) {
    czas_t a, b; int wa;
    surowy(&a, &wa);
    do { b = a; surowy(&a, &wa); }
    while (a.sek != b.sek || a.min != b.min || a.godz != b.godz || a.dzien != b.dzien);

    u8 regb = cmos(0x0B);
    int pm = a.godz & 0x80;
    a.godz &= 0x7F;
    if (!(regb & 0x04)) {
        a.sek = bcd(a.sek); a.min = bcd(a.min); a.godz = bcd(a.godz);
        a.dzien = bcd(a.dzien); a.mies = bcd(a.mies); a.rok = bcd(a.rok);
        wa = bcd(wa);
    }
    if (!(regb & 0x02)) {               /* tryb 12-godzinny */
        if (a.godz == 12) a.godz = 0;
        if (pm) a.godz += 12;
    }
    a.rok += (wa >= 19 && wa <= 30) ? wa * 100 : 2000;

    /* przesunięcie strefy czasowej z przeniesieniem daty */
    if (strefa_godzin) {
        int dni = dni_od_epoki(a.rok, a.mies, a.dzien);
        int minuty = a.godz * 60 + a.min + strefa_godzin * 60;
        while (minuty < 0) { minuty += 1440; dni--; }
        while (minuty >= 1440) { minuty -= 1440; dni++; }
        data_z_dni(dni, &a.rok, &a.mies, &a.dzien);
        a.godz = minuty / 60; a.min = minuty % 60;
    }
    *c = a;
}

void czekaj_sekundy(int n) {
    czas_t c; rtc_czytaj(&c);
    int ost = c.sek;
    while (n > 0) {
        if (kbd_poll() >= 0) return;
        rtc_czytaj(&c);
        if (c.sek != ost) { ost = c.sek; n--; }
    }
}
