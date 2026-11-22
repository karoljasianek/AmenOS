/* AmenOS - dekodery obrazków */
#ifndef AMENOS_OBRAZY_H
#define AMENOS_OBRAZY_H
#include "kernel.h"

typedef struct { u8 *pam; u32 rozmiar, zajete; } arena_t;

extern const char *obraz_blad;
/* PNG, GIF, JPEG (baseline), BMP -> piksele 0x00RRGGBB w arenie; 1 = sukces */
int  obraz_dekoduj(const u8 *dane, u32 n, arena_t *a, u32 **piks, int *w, int *h);
void obraz_skaluj(const u32 *z, int zw, int zh, u32 *d, int dw, int dh);

#endif
