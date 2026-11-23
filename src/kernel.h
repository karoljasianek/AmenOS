/* AmenOS - wspólny nagłówek jądra */
#ifndef AMENOS_KERNEL_H
#define AMENOS_KERNEL_H

#define AMENOS_WERSJA "2.0"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    s8;

/* ---------- porty we/wy ---------- */
static inline void outb(u16 port, u8 val) { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline void outw(u16 port, u16 val) { __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port)); }
static inline u16 inw(u16 port) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline u8 inb(u16 port) { u8 v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outl(u16 port, u32 val) { __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port)); }
static inline u32 inl(u16 port) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }

/* ---------- biblioteka (lib.c) ---------- */
void *memset(void *dst, int c, u32 n);
void *memcpy(void *dst, const void *src, u32 n);
u32  k_strlen(const char *s);
int  k_strcmp(const char *a, const char *b);
void k_strncpy(char *dst, const char *src, u32 max);   /* zawsze kończy zerem */
int  k_atoi(const char *s, int *ok);
char *k_token(char **p);                               /* kolejne słowo, NULL gdy koniec */
void k_fold_pl(char *dst, const char *src, u32 max);   /* małe litery, bez ogonków */
u32  k_utf8_next(const char **s);                      /* dekoduje jeden znak UTF-8 */
u32  k_utf8_len(const char *s);                        /* liczba znaków (nie bajtów) */
int  k_utf8_put(u32 c, char *out);                     /* koduje znak, zwraca liczbę bajtów */
int  k_memcmp(const void *a, const void *b, u32 n);

/* ---------- ekran (vga.c) ---------- */
enum {
    CZARNY = 0, NIEBIESKI, ZIELONY, TURKUSOWY, CZERWONY, FIOLET, BRAZOWY, SZARY,
    CIEMNOSZARY, JASNONIEBIESKI, JASNOZIELONY, JASNOTURKUSOWY, JASNOCZERWONY,
    ROZOWY, ZOLTY, BIALY
};
#define VGA_W 80
#define VGA_H 25
#define ATTR(fg, bg) ((u8)(((bg) << 4) | (fg)))

void vga_init(int grafika);
u16 *vga_bufor(void);
int  vga_kursor_widoczny(void);
void vga_clear(void);
void vga_color(u8 fg, u8 bg);
u8   vga_attr(void);
void vga_goto(int x, int y);
int  vga_x(void);
int  vga_y(void);
void vga_cursor(int on);
void vga_putc(u32 codepoint);
void vga_puts(const char *s);
void vga_glyph_at(int x, int y, u8 glyph, u8 attr);
void vga_puts_at(int x, int y, const char *s, u8 attr);
void vga_fill(int x, int y, int w, int h, u8 glyph, u8 attr);
void vga_backspace(void);
u8   vga_map(u32 codepoint);
void kprintf(const char *fmt, ...);
void ksprintf(char *out, u32 max, const char *fmt, ...);

/* ---------- klawiatura (klawiatura.c) ---------- */
#define KEY_UP     0x1001
#define KEY_DOWN   0x1002
#define KEY_LEFT   0x1003
#define KEY_RIGHT  0x1004
#define KEY_PGUP   0x1005
#define KEY_PGDN   0x1006
#define KEY_HOME   0x1007
#define KEY_END    0x1008
#define KEY_ESC    27
#define KEY_ENTER  '\n'
#define KEY_BKSP   '\b'
#define KEY_F1     0x1011
#define KEY_F10    0x1012

int  kbd_poll(void);                    /* -1 gdy brak klawisza */
void ps2_obsluz(void);                  /* odbiera bajty z portu 8042 (klawiatura + mysz) */
void mysz_init(void);
void kbd_wstrzyknij(const char *s);     /* wpisuje tekst do terminala, jakby z klawiatury */
void kbd_wloz_kod(u8 kod);              /* kod klawisza PS/2 (np. z klawiatury USB) do kolejki */

/* ---------- USB (usb.c, xhci.c, uhci.c) ---------- */
void usb_init(void);
void usb_odpytaj(void);
void cmd_usb(void);
extern int usb_wylaczone;
int  kbd_getkey(void);                  /* czeka na klawisz */
int  kbd_readline(char *buf, int max);  /* linia z historią, UTF-8; -1 = przerwano (F10) */
extern int kbd_historia;

/* ---------- czas i dźwięk (dzwiek.c) ---------- */
void czasomierz_init(void);
u32  zegar_ms(void);
void czekaj_ms(u32 ms);
void glosnik_ton(u32 hz);
void glosnik_cisza(void);

/* ---------- zegar (rtc.c) ---------- */
typedef struct { int rok, mies, dzien, godz, min, sek; } czas_t;
extern int strefa_godzin;
void rtc_czytaj(czas_t *c);             /* z uwzględnieniem strefy */
void czekaj_sekundy(int n);             /* przerywane klawiszem */

/* ---------- kalendarz liturgiczny (liturgia.c) ---------- */
enum { KOL_ZIELONY, KOL_FIOLETOWY, KOL_BIALY, KOL_CZERWONY, KOL_ROZOWY };

typedef struct {
    char nazwa[96];        /* główny obchód dnia */
    char uwaga[96];        /* wspomnienie / informacja dodatkowa */
    const char *okres;     /* okres liturgiczny */
    int kolor;             /* KOL_* */
    int ranga;             /* 1 = najwyższa ... 7 = dzień powszedni */
    char cykl_niedz;       /* 'A' / 'B' / 'C' */
    int  cykl_powsz;       /* 1 / 2 */
} dzien_lit_t;

int  dni_od_epoki(int r, int m, int d);           /* 1970-01-01 = 0 */
void data_z_dni(int z, int *r, int *m, int *d);
int  dzien_tygodnia(int dni);                      /* 0 = niedziela */
int  dni_w_miesiacu(int r, int m);
int  wielkanoc(int rok);                           /* w dniach od epoki */
int  adwent1(int rok);
void liturgia(int r, int m, int d, dzien_lit_t *out);
const char *kolor_nazwa(int k);
u8   kolor_vga(int k);
u8   kolor_tekst(int k);

extern const char *dni_tyg[7];      /* "niedziela" ... */
extern const char *dni_tyg_wielk[7];/* "Niedziela" ... */
extern const char *mies_dop[13];    /* "stycznia" ... */
extern const char *mies_mian[13];   /* "Styczeń" ... */
extern const char *mies_mian_wielk[13]; /* "STYCZEŃ" ... */

/* ---------- karta sieciowa (e1000.c) ---------- */
extern u8 karta_mac[6];
extern int karta_jest;
int  e1000_init(void);
int  e1000_lacze(void);
void e1000_wyslij(const void *dane, u32 dl);
int  e1000_odbierz(u8 *wy);              /* 0 = brak, -1 = ramka błędna, >0 = długość */
const char *karta_nazwa(void);

/* ---------- sieć (siec.c) ---------- */
extern u32 moje_ip, maska, brama, dns_ip, dns_wlasny;
extern int siec_gotowa;
int  siec_start(void);                   /* 0 = OK, <0 = błąd (siec_blad) */
const char *siec_blad(int kod);
void siec_odbieraj(void);
u32  losowa(void);
void ip_tekst(u32 ip, char *b);
int  ip_z_tekstu(const char *s, u32 *ip);
int  dns_zapytaj(const char *nazwa, u32 *ip);
int  ping(u32 cel, u32 *ms);
int  tcp_polacz(u32 ip, u16 port, u8 *bufor, u32 rozmiar);
int  tcp_wyslij(const void *dane, u32 dl);
u32  tcp_odebrano(void);
int  tcp_zakonczone(void);
u32  tcp_cisza_ms(void);
void tcp_zamknij(void);

/* ---------- przeglądarka (przegladarka.c) ---------- */
void przegladarka(const char *adres);
void cmd_siec(char *p);
void cmd_ping(char *arg);

/* ---------- grafika (fb.c) ---------- */
#define FB_PRZEZR 0xFF000000u
extern int fb_jest, fb_w, fb_h;
int  fb_init(const u32 *mbi);
void fb_obraz(int x, int y, int w, int h, const u32 *piks);
void *pamiec_duza(u32 rozmiar);          /* kmain.c: duży obszar za jądrem albo 0 */
void fb_klip(int x, int y, int w, int h);
void fb_klip_caly(void);
void fb_klip_zawez(int x, int y, int w, int h);
void fb_klip_pobierz(int *x, int *y, int *w, int *h);
void fb_piksel(int x, int y, u32 c);
u32  fb_mieszaj(u32 a, u32 b, int alfa);
void fb_prost(int x, int y, int w, int h, u32 c);
void fb_prost_alfa(int x, int y, int w, int h, u32 c, int alfa);
void fb_prost_zaokr(int x, int y, int w, int h, int r, u32 c);
void fb_ramka(int x, int y, int w, int h, u32 c);
void fb_gradient(int x, int y, int w, int h, u32 c1, u32 c2);
void fb_linia(int x0, int y0, int x1, int y1, u32 c, int grubosc);
void fb_kolo(int cx, int cy, int r, u32 c);
void fb_okrag(int cx, int cy, int r, u32 c, int grubosc);
void fb_znak(int x, int y, u8 g, u32 kolor, u32 tlo, int skala);
int  fb_tekst(int x, int y, const char *s, u32 kolor, u32 tlo, int skala);
int  fb_tekst_szer(const char *s, int skala);
int  fb_tekst_zawin(int x, int y, const char *s, int max_znakow, u32 kolor, int odstep);
void fb_ikona(int x, int y, const u32 *piks, int skala);
const u32 *fb_ikona_nr(int nr);
int  fb_sinus(int i);                   /* i = 0..59, wynik * 1000 */
void fb_wyslij(int x, int y, int w, int h);

/* ---------- dane parafii (powloka.c) - wspólne dla terminala i okien ---------- */
extern char parafia[64];
#define PIESNI_N 6
extern const char *piesni_etyk[PIESNI_N];
extern char piesni[PIESNI_N][6];
#define MSZE_N 4
typedef struct { char etyk[32]; char godz[64]; } msza_t;
extern msza_t msze[MSZE_N];
#define OGL_N 12
extern char ogloszenia[OGL_N][128];
extern int ogl_n;
typedef struct { const char *tytul; const char *linie[20]; } modlitwa_t;
extern const modlitwa_t modlitwy[];
extern const int modlitwy_n;
typedef struct { const char *klucze[12]; const char *nauka; const char *pokuta; } temat_t;
extern const temat_t sp_tematy[11];
extern const char *AKT_ZALU, *ROZGRZESZENIE;
int zawiera(const char *h, const char *n);

/* nowoczesny wygląd: czcionki wygładzane (kolejność jak w tools/gen_wyglad.py) */
enum { F_MALY, F_TEKST, F_TEKST_B, F_WWW, F_WWW_B, F_SREDNI, F_H2, F_DUZY, F_WIELKI, F_OGROMNY, F_N };
int  fb_napis(int x, int y, const char *s, u32 kolor, int f);        /* y = górna krawędź wiersza */
int  fb_napis_n(int x, int y, const char *s, int bajty, u32 kolor, int f);
int  fb_napis_szer(const char *s, int f);
int  fb_napis_szer_n(const char *s, int bajty, int f);
int  fb_znak_szer(u32 cp, int f);
int  fb_napis_wys(int f);
int  fb_napis_asc(int f);
void fb_napis_srodek(int x, int w, int y, const char *s, u32 kolor, int f);
int  fb_napis_zawin(int x, int y, int w, const char *s, u32 kolor, int f, int odstep);  /* kolor FB_PRZEZR = tylko policz */
void fb_napis_przytnij(char *o, u32 rozm, const char *s, int maxw, int f);
void fb_term_znak(int x, int y, u8 g, u32 kolor, u32 tlo);
#define TERM_W 10
#define TERM_H 20
void fb_obraz_alfa(int x, int y, int w, int h, const u32 *argb, int alfa);
void fb_ikona_rys(int x, int y, int nr, int rozm);                  /* rozm 48, 32 albo 24 */
void fb_zaokr(int x, int y, int w, int h, int r, u32 c, int alfa);  /* alfa 0..256 */
void fb_zaokr_ramka(int x, int y, int w, int h, int r, u32 c, int alfa);
void fb_zaokr_gradient(int x, int y, int w, int h, int r, u32 c1, u32 c2);
void fb_cien(int x, int y, int w, int h, int r, int rozmiar, int sila, int przesun);
void fb_kolo_aa(int cx, int cy, int r, u32 c, int alfa);
void fb_okrag_aa(int cx, int cy, int r, int grub, u32 c, int alfa);
void fb_linia_aa(int x0, int y0, int x1, int y1, int grub, u32 c);
void fb_tapeta_init(void);
void fb_tapeta(int x, int y, int w, int h);
void fb_kursor(int x, int y);
#define KURSOR_SZ 24
#define KURSOR_WYS 32
void fb_rogi_zapisz(int x, int y, int w, int h, int r);
void fb_rogi_przywroc(int x, int y, int w, int h, int r, u32 ramka);

/* ---------- pulpit (gui.c) ---------- */
extern int gui_aktywne;
extern int powloka_czeka;               /* powłoka czeka na polecenie */
void gui_init(u32 pamiec_kb);
void gui_powitanie(void);
void gui_pompuj(void);
void gui_mysz(int dx, int dy, int przyciski);
void gui_klawisz(int k);
int  gui_terminal_aktywny(void);

/* ---------- muzyka (wspólne) ---------- */
typedef struct { const char *tytul, *czesc, *opis; const u8 *nuty; const u16 *czasy; int n; } utwor_t;

/* ---------- powłoka (powloka.c) ---------- */
void powloka(u32 mem_kb);
void ekran_powitalny(void);

#endif
