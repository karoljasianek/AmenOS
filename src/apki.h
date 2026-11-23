/* AmenOS - programy graficzne (aplikacje.c) widziane przez pulpit (gui.c) */
#ifndef APKI_H
#define APKI_H

enum { AP_PIESNI, AP_MODLITWY, AP_SPOWIEDZ, AP_KOMUNIA, AP_OGLOSZENIA, AP_MSZE, AP_ZADANIA, AP_N };

typedef struct {
    const char *tytul;
    int ikona;
    int cw, ch;                          /* rozmiar wnętrza okna */
    void (*rysuj)(int x, int y);
    void (*klik)(int lx, int ly);
    int  (*klawisz)(int k);              /* 1 = obsłużony; nieobsłużony Esc zamyka okno */
    void (*tik)(u32 t);                  /* wołane stale, także gdy okno jest w tle */
    void (*otwarte)(void);
    void (*zamkniete)(void);
} apka_t;
extern const apka_t apki[AP_N];

/* usługi pulpitu dla programów */
void gui_apka_brudne(int a, int x, int y, int w, int h);
void gui_apka_cala(int a);
int  gui_apka_otwarta(int a);
void gui_apka_zamknij(int a);
u32  gui_akcent(void);
u32  gui_kolor_lit(int k);
void gui_muzyka_graj(int i);
void gui_muzyka_stop(void);
void gui_komunikat(const char *s);
/* dla menedżera zadań */
int  gui_okna_lista(int *id, int max);
const char *gui_okno_tytul(int id);
int  gui_okno_ikona(int id);
int  gui_okno_stan(int id);               /* 0 aktywne, 1 w tle, 2 zminimalizowane */
int  gui_okno_pamiec_kb(int id);
void gui_okno_przelacz(int id);
void gui_okno_zamknij_id(int id);

#endif
