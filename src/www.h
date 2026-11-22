/* AmenOS - wspólne elementy przeglądarek (tekstowej i graficznej) */
#ifndef AMENOS_WWW_H
#define AMENOS_WWW_H
#include "kernel.h"

#define ADR      400
#define START    "about:start"
#define FROGFIND "http://frogfind.com"

enum { UTF8, ISO2, CP1250, LATIN1 };

typedef struct {
    char url[ADR];              /* adres po przekierowaniach */
    int kod;                    /* kod HTTP */
    char typ[96];               /* Content-Type */
    u8 *cialo;                  /* treść (zakończona zerem) */
    u32 dl;
    char blad_tytul[64], blad[200];
} www_odp_t;

/* pobiera adres do bufora; postep(msg) - komunikat (msg może być 0), zwraca !=0 aby przerwać */
int  www_pobierz(const char *url, u8 *bufor, u32 rozmiar, www_odp_t *o, int (*postep)(const char *msg));

char mala(char c);
int  zaczyna(const char *s, const char *p);
const u8 *szukaj_ci(const u8 *s, const u8 *k, const char *wzor);
void dopisz(char *d, u32 max, const char *s);
void url_koduj(const char *s, char *o, u32 max);
void przez_frogfind(const char *https, char *o, u32 max);
int  url_pelny(const char *baza, const char *href, char *o, u32 max);
void wpisany_adres(const char *s, char *o, u32 max);
extern int kodowanie;
int  ustal_kodowanie(const char *typ, const u8 *cialo, u32 dl);
u32  znak_z(const u8 **pp, const u8 *k);
u32  encja(const u8 **pp, const u8 *k);
int  atrybut(const u8 *s, const u8 *e, const char *nazwa, char *o, u32 max);
extern const char *STRONA_STARTOWA;

#endif
