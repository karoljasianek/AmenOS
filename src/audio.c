/* AmenOS - karta dźwiękowa: Intel High Definition Audio i AC'97.
 *
 * Bez przerwań: karta w kółko odtwarza bufor-pierścień (DMA), a audio_pompuj() - wołane przy
 * każdej okazji (klawiatura, pulpit, czekanie) - sprawdza, gdzie karta właśnie gra, i dolewa
 * świeże próbki z syntezatora ok. 120 ms przed nią. Format: 48 kHz, 16 bitów, stereo.
 * Bez karty dźwiękowej system dalej używa głośniczka PC. */
#include "kernel.h"
#include "usb.h"                                    /* pci_czytaj, pci_pisz */

int audio_jest;
char audio_opis[80] = "brak (głośniczek PC)";
int audio_krok;                                     /* do diagnostyki: dokąd doszła inicjalizacja */

#define BAJTY_MS 192                                /* 48 próbek x 2 kanały x 2 bajty */
static u32 ZAPAS = 120 * BAJTY_MS;                  /* tyle dźwięku trzymamy przed kartą */

static u8 *pierscien;
static u32 cbl, wpoz, zpoz;                        /* zpoz: dokąd przed nami są już zera */
static int rodzaj;                                  /* 1 HDA, 2 AC'97 */

static void *pamiec_wyr(u32 rozm, u32 wyr) {
    u8 *p = pamiec_duza(rozm + wyr);
    if (!p) return 0;
    p = (u8 *)(((u32)p + wyr - 1) & ~(wyr - 1));
    memset(p, 0, rozm);
    return p;
}

static int pci_szukaj(int klasa, int podklasa, int *b, int *d, int *f) {
    for (int bus = 0; bus < 256; bus++)
        for (int dev = 0; dev < 32; dev++)
            for (int fn = 0; fn < 8; fn++) {
                u32 id = pci_czytaj(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (!fn) break; continue; }
                u32 kl = pci_czytaj(bus, dev, fn, 8);
                if ((int)(kl >> 24) == klasa && (int)((kl >> 16) & 0xFF) == podklasa) { *b = bus; *d = dev; *f = fn; return 1; }
                if (!fn && !(pci_czytaj(bus, dev, 0, 0x0C) & 0x800000)) break;    /* urządzenie jednofunkcyjne */
            }
    return 0;
}

/* ====================================================================== */
/*  Intel High Definition Audio                                            */
/* ====================================================================== */

static volatile u8 *hda;
#define R8(o)  (*(volatile u8  *)(hda + (o)))
#define R16(o) (*(volatile u16 *)(hda + (o)))
#define R32(o) (*(volatile u32 *)(hda + (o)))

static volatile u32 *corb, *rirb;
static int corb_n, rirb_n, rirb_rp, natychm;
static u32 sd;                                       /* rejestry strumienia wyjściowego */

static int czekaj_bit8(u32 rej, u8 maska, int stan, u32 ms) {
    u32 t0 = zegar_ms();
    while (((R8(rej) & maska) != 0) != stan) if (zegar_ms() - t0 > ms) return 0;
    return 1;
}

static int hda_polecenie(u32 v, u32 *odp) {
    u32 t0;
    if (natychm) {                                   /* interfejs natychmiastowy (zapasowy) */
        t0 = zegar_ms();
        while (R16(0x68) & 1) if (zegar_ms() - t0 > 20) return 0;
        R16(0x68) = 2;
        R32(0x60) = v;
        R16(0x68) = 1;
        t0 = zegar_ms();
        while (!(R16(0x68) & 2)) if (zegar_ms() - t0 > 20) return 0;
        *odp = R32(0x64);
        return 1;
    }
    int wp = ((R16(0x48) & 0xFF) + 1) % corb_n;
    corb[wp] = v;
    R16(0x48) = (u16)wp;
    t0 = zegar_ms();
    while ((R16(0x58) & 0xFF) == rirb_rp) if (zegar_ms() - t0 > 30) return 0;
    rirb_rp = (rirb_rp + 1) % rirb_n;
    *odp = rirb[rirb_rp * 2];
    return 1;
}

static int kodek;
static u32 hv(int nid, u32 czas, u32 dane) {          /* czasownik 12-bitowy albo 4-bitowy */
    u32 v = ((u32)kodek << 28) | ((u32)nid << 20);
    v |= czas > 0xF ? (czas << 8) | (dane & 0xFF) : (czas << 16) | (dane & 0xFFFF);
    u32 o = 0;
    if (!hda_polecenie(v, &o)) return 0xFFFFFFFF;
    return o;
}
static u32 par(int nid, int p) { return hv(nid, 0xF00, (u32)p); }

#define WMAX 96
static struct { u32 caps, pin, pincaps, ampout; u8 typ, nconn, conn[16]; } wd[WMAX];
static int wpocz, wile, afg;

static int w_ok(int nid) { return nid >= wpocz && nid < wpocz + wile && nid - wpocz < WMAX; }
#define W(n) wd[(n) - wpocz]

static void wczytaj_polaczenia(int nid) {
    u32 len = par(nid, 0x0E);
    int dlugie = (len >> 7) & 1, n = (int)(len & 0x7F), k = 0, ost = 0;
    for (int i = 0; i < n && k < 16; i += dlugie ? 2 : 4) {
        u32 r = hv(nid, 0xF02, (u32)i);
        for (int j = 0; j < (dlugie ? 2 : 4) && i + j < n && k < 16; j++) {
            u32 e = dlugie ? (r >> (16 * j)) & 0xFFFF : (r >> (8 * j)) & 0xFF;
            int zakres = dlugie ? (e >> 15) & 1 : (e >> 7) & 1;
            int c = (int)(e & (dlugie ? 0x7FFF : 0x7F));
            if (zakres && ost) for (int m = ost + 1; m <= c && k < 16; m++) W(nid).conn[k++] = (u8)m;
            else W(nid).conn[k++] = (u8)c;
            ost = c;
        }
    }
    W(nid).nconn = (u8)k;
}

static void wzmocnienie(int nid, int wyjscie, int indeks) {
    u32 kaps = W(nid).ampout;
    if (!wyjscie || !(W(nid).caps & 0x8)) kaps = par(afg, wyjscie ? 0x12 : 0x0D);
    if (W(nid).caps & (wyjscie ? 0x4 : 0x2)) {
        u32 kroki = (kaps >> 8) & 0x7F, zero = kaps & 0x7F, g = zero ? zero : kroki;
        if (!wyjscie) { u32 k2 = par(nid, 0x0D); g = (k2 & 0x7F) ? (k2 & 0x7F) : ((k2 >> 8) & 0x7F); }
        hv(nid, 0x3, (wyjscie ? 0xB000u : 0x7000u) | ((u32)indeks << 8) | g);
    }
}

/* ścieżka od wyjścia do przetwornika DAC: przez miksery i selektory, najwyżej 5 kroków */
static int sciezka[8], wybor[8], dl_sc;
static int szukaj_dac(int nid, int gl) {
    if (!w_ok(nid) || gl > 5) return 0;
    sciezka[gl] = nid;
    if (W(nid).typ == 0) { dl_sc = gl + 1; return 1; }
    if (gl && W(nid).typ != 2 && W(nid).typ != 3) return 0;
    for (int i = 0; i < W(nid).nconn; i++) { wybor[gl] = i; if (szukaj_dac(W(nid).conn[i], gl + 1)) return 1; }
    return 0;
}

static int hda_kodek(int cad, int *dacow, int *wyjsc, u32 *nazwa) {
    kodek = cad;
    *nazwa = par(0, 0);
    u32 sub = par(0, 4);
    if (sub == 0xFFFFFFFF) return 0;
    afg = 0;
    for (int fg = (int)((sub >> 16) & 0xFF), k = 0; k < (int)(sub & 0xFF); k++, fg++)
        if ((par(fg, 5) & 0xFF) == 1) { afg = fg; break; }
    if (!afg) return 0;
    hv(afg, 0x705, 0);                               /* zasilanie D0 */
    czekaj_ms(10);
    sub = par(afg, 4);
    wpocz = (int)((sub >> 16) & 0xFF); wile = (int)(sub & 0xFF);
    if (wile > WMAX) wile = WMAX;
    for (int n = wpocz; n < wpocz + wile; n++) {
        W(n).caps = par(n, 9);
        W(n).typ = (u8)((W(n).caps >> 20) & 0xF);
        W(n).ampout = par(n, 0x12);
        if (W(n).caps & 0x100) wczytaj_polaczenia(n); else W(n).nconn = 0;
        if (W(n).typ == 4) { W(n).pin = hv(n, 0xF1C, 0); W(n).pincaps = par(n, 0x0C); }
    }
    *dacow = *wyjsc = 0;
    for (int n = wpocz; n < wpocz + wile; n++) {
        if (W(n).typ != 4 || !(W(n).pincaps & 0x10)) continue;      /* tylko wyjścia */
        u32 polacz = W(n).pin >> 30, urz = (W(n).pin >> 20) & 0xF;
        if (polacz == 1) continue;                                   /* niepodłączone gniazdo */
        if (urz != 0 && urz != 1 && urz != 2) continue;              /* line out, głośnik, słuchawki */
        if (!szukaj_dac(n, 0)) continue;
        for (int i = 0; i < dl_sc; i++) {
            int w = sciezka[i];
            hv(w, 0x705, 0);
            if (i < dl_sc - 1) {
                if (W(w).typ == 2) wzmocnienie(w, 0, wybor[i]);
                else if (W(w).nconn > 1) hv(w, 0x701, (u32)wybor[i]);
            }
            wzmocnienie(w, 1, 0);
        }
        int dac = sciezka[dl_sc - 1];
        hv(dac, 0x706, 1 << 4);                                     /* strumień 1, kanał 0 */
        hv(dac, 0x2, 0x0011);                                        /* 48 kHz, 16 bitów, stereo */
        hv(n, 0x707, 0x40 | ((W(n).pincaps & 0x8) ? 0x80 : 0));      /* włącz wyjście (+ wzmacniacz słuchawek) */
        if (W(n).pincaps & 0x10000) hv(n, 0x70C, 2);                 /* EAPD - zewnętrzny wzmacniacz */
        (*wyjsc)++;
        (*dacow)++;
    }
    return *wyjsc;
}

static int hda_init(int b, int d, int f) {
    u32 bar = pci_czytaj(b, d, f, 0x10);
    if (bar & 1) { audio_krok = 11; return 0; }
    if (((bar >> 1) & 3) == 2 && pci_czytaj(b, d, f, 0x14)) { audio_krok = 12; return 0; }   /* powyżej 4 GB - niedostępne bez stronicowania */
    hda = (volatile u8 *)(bar & ~0xFu);
    pci_pisz(b, d, f, 4, pci_czytaj(b, d, f, 4) | 0x6);              /* pamięć + bus master */
    u32 id = pci_czytaj(b, d, f, 0);
    if ((id & 0xFFFF) == 0x8086) pci_pisz(b, d, f, 0x44, pci_czytaj(b, d, f, 0x44) & ~7u);   /* TCSEL = 0 */

    /* reset kontrolera */
    R8(0x4C) = 0; R8(0x5C) = 0;
    R32(0x08) &= ~1u;
    u32 t0 = zegar_ms();
    while (R32(0x08) & 1) if (zegar_ms() - t0 > 100) { audio_krok = 13; return 0; }
    czekaj_ms(1);
    R32(0x08) |= 1;
    t0 = zegar_ms();
    while (!(R32(0x08) & 1)) if (zegar_ms() - t0 > 100) { audio_krok = 14; return 0; }
    czekaj_ms(20);                                   /* kodeki zgłaszają się w STATESTS */
    u16 kodeki = R16(0x0E);
    if (!kodeki) { audio_krok = 15; return 0; }

    /* CORB i RIRB */
    corb = pamiec_wyr(1024, 128); rirb = pamiec_wyr(2048, 128);
    if (!corb || !rirb) { audio_krok = 16; return 0; }
    u8 cs = R8(0x4E), rs = R8(0x5E);
    corb_n = (cs & 0x40) ? 256 : (cs & 0x20) ? 16 : 2;
    rirb_n = (rs & 0x40) ? 256 : (rs & 0x20) ? 16 : 2;
    R8(0x4E) = (u8)((cs & 0xF0) | (corb_n == 256 ? 2 : corb_n == 16 ? 1 : 0));
    R8(0x5E) = (u8)((rs & 0xF0) | (rirb_n == 256 ? 2 : rirb_n == 16 ? 1 : 0));
    R32(0x40) = (u32)corb; R32(0x44) = 0;
    R32(0x50) = (u32)rirb; R32(0x54) = 0;
    R16(0x4A) = 0x8000;                              /* reset wskaźnika odczytu CORB */
    t0 = zegar_ms();
    while (!(R16(0x4A) & 0x8000) && zegar_ms() - t0 < 10) ;
    R16(0x4A) = 0;
    t0 = zegar_ms();
    while ((R16(0x4A) & 0x8000) && zegar_ms() - t0 < 10) ;
    R16(0x48) = 0;
    R16(0x58) = 0x8000;                              /* reset wskaźnika zapisu RIRB */
    R16(0x5A) = 1;
    rirb_rp = 0;
    R8(0x4C) = 2; R8(0x5C) = 2;                      /* uruchom DMA kolejek */
    czekaj_ms(2);

    /* kodeki: pierwszy z wyjściem audio wygrywa */
    int dacow = 0, wyjsc = 0;
    u32 nazwa = 0;
    int jest = 0;
    for (int proba = 0; proba < 2 && !jest; proba++) {
        if (proba) { natychm = 1; R8(0x4C) = 0; R8(0x5C) = 0; }    /* CORB nie odpowiada: interfejs natychmiastowy */
        for (int c = 0; c < 15 && !jest; c++)
            if (kodeki & (1 << c)) jest = hda_kodek(c, &dacow, &wyjsc, &nazwa);
    }
    if (!jest) { audio_krok = 17; return 0; }

    /* strumień wyjściowy nr 1 (pierwszy deskryptor wyjściowy) */
    u16 gcap = R16(0x00);
    sd = 0x80 + ((gcap >> 8) & 0xF) * 0x20;
    cbl = 48000 * 4;                                 /* 1 s */
    pierscien = pamiec_wyr(cbl, 4096);
    u32 *bdl = pamiec_wyr(16 * 4, 128);
    if (!pierscien || !bdl) { audio_krok = 18; return 0; }
    for (int i = 0; i < 4; i++) { bdl[i * 4] = (u32)pierscien + (u32)i * (cbl / 4); bdl[i * 4 + 1] = 0; bdl[i * 4 + 2] = cbl / 4; bdl[i * 4 + 3] = 0; }
    R8(sd) = 0;
    czekaj_bit8(sd, 2, 0, 20);
    R8(sd) = 1;
    czekaj_bit8(sd, 1, 1, 20);
    R8(sd) = 0;
    czekaj_bit8(sd, 1, 0, 20);
    R8(sd + 2) = 1 << 4;                             /* numer strumienia 1 */
    R32(sd + 0x08) = cbl;
    R16(sd + 0x0C) = 3;
    R16(sd + 0x12) = 0x0011;
    R32(sd + 0x18) = (u32)bdl; R32(sd + 0x1C) = 0;
    R8(sd) = 2;                                      /* start */

    ksprintf(audio_opis, sizeof audio_opis, "Intel HD Audio, kodek %04x:%04x, wyjść: %d", nazwa >> 16, nazwa & 0xFFFF, wyjsc);
    return 1;
}

static u32 hda_pozycja(void) { return R32(sd + 0x04) % cbl; }

/* ====================================================================== */
/*  AC'97 (Intel ICH i zgodne)                                             */
/* ====================================================================== */

static u16 nam, nabm;
#define AC_BUF 4096                                  /* 32 bufory po 4096 bajtów = 683 ms */

static int ac97_init(int b, int d, int f) {
    u32 b0 = pci_czytaj(b, d, f, 0x10), b1 = pci_czytaj(b, d, f, 0x14);
    if (!(b0 & 1) || !(b1 & 1)) return 0;
    nam = (u16)(b0 & ~3u); nabm = (u16)(b1 & ~3u);
    pci_pisz(b, d, f, 4, pci_czytaj(b, d, f, 4) | 0x5);              /* porty + bus master */
    outl(nabm + 0x2C, 0x2);                          /* koniec zimnego resetu */
    u32 t0 = zegar_ms();
    while (!(inl(nabm + 0x30) & 0x100)) if (zegar_ms() - t0 > 600) return 0;
    outw(nam + 0x00, 0);
    czekaj_ms(10);
    outw(nam + 0x02, 0x0000);                        /* głośność główna 0 dB */
    outw(nam + 0x04, 0x0000);                        /* słuchawki */
    outw(nam + 0x18, 0x0808);                        /* PCM 0 dB */
    if (inw(nam + 0x28) & 1) { outw(nam + 0x2A, inw(nam + 0x2A) | 1); outw(nam + 0x2C, 48000); }
    cbl = 32 * AC_BUF;
    pierscien = pamiec_wyr(cbl, 4096);
    u32 *bdl = pamiec_wyr(32 * 8, 8);
    if (!pierscien || !bdl) return 0;
    for (int i = 0; i < 32; i++) { bdl[i * 2] = (u32)pierscien + (u32)i * AC_BUF; bdl[i * 2 + 1] = AC_BUF / 2; }
    outb(nabm + 0x1B, 2);
    t0 = zegar_ms();
    while ((inb(nabm + 0x1B) & 2) && zegar_ms() - t0 < 20) ;
    outl(nabm + 0x10, (u32)bdl);
    outb(nabm + 0x15, 31);
    outb(nabm + 0x1B, 1);
    u16 v1 = inw(nam + 0x7C), v2 = inw(nam + 0x7E);
    ksprintf(audio_opis, sizeof audio_opis, "AC'97, kodek %04x:%04x", v1, v2);
    return 1;
}

static u32 ac97_pozycja(void) {
    u8 civ = inb(nabm + 0x14) & 31;
    u16 picb = inw(nabm + 0x18);
    outb(nabm + 0x15, (u8)((civ + 31) & 31));        /* karta nigdy nie dojdzie do końca listy */
    if (inw(nabm + 0x16) & 1) { outw(nabm + 0x16, 0x1C); outb(nabm + 0x1B, 1); }   /* zatrzymała się - wznów */
    u32 p = (u32)civ * AC_BUF + AC_BUF - (u32)picb * 2;
    return p % cbl;
}

/* ====================================================================== */

int audio_init(void) {
    int b, d, f;
    audio_krok = 1;
    if (pci_szukaj(4, 3, &b, &d, &f) && (audio_krok = 2) && hda_init(b, d, f)) rodzaj = 1;
    else if (pci_szukaj(4, 1, &b, &d, &f) && ac97_init(b, d, f)) { rodzaj = 2; ZAPAS = 250 * BAJTY_MS; }
    else return 0;
    wpoz = zpoz = (rodzaj == 1 ? hda_pozycja() : ac97_pozycja()) & ~3u;
    audio_jest = 1;
    return 1;
}

void audio_pompuj(void) {
    static int w_pompie;
    if (!audio_jest || w_pompie) return;
    w_pompie = 1;
    u32 r = (rodzaj == 1 ? hda_pozycja() : ac97_pozycja()) & ~3u;
    u32 przed = (wpoz + cbl - r) % cbl;
    if (przed > cbl / 2) { wpoz = (r + 20 * BAJTY_MS) % cbl; przed = 20 * BAJTY_MS; zpoz = wpoz; }   /* karta nas dogoniła */
    while (przed < ZAPAS) {
        u32 n = ZAPAS - przed;
        if (n > 1024) n = 1024;
        if (n > cbl - wpoz) n = cbl - wpoz;
        synteza_generuj((short *)(pierscien + wpoz), (int)(n / 4));
        wpoz = (wpoz + n) % cbl;
        przed += n;
    }
    /* za świeżymi próbkami trzymamy ok. 400 ms ciszy - gdyby system na chwilę przestał dolewać,
       karta zagra ciszę zamiast powtarzać to, co było w pierścieniu sekundę temu */
    u32 zera = (zpoz + cbl - wpoz) % cbl;
    if (zera > cbl / 2) { zpoz = wpoz; zera = 0; }
    while (zera < 300 * BAJTY_MS && zera < cbl - ZAPAS - 8192) {
        u32 n = 400 * BAJTY_MS - zera;
        if (n > cbl - zpoz) n = cbl - zpoz;
        memset(pierscien + zpoz, 0, n);
        zpoz = (zpoz + n) % cbl;
        zera += n;
    }
    w_pompie = 0;
}

void audio_diagnoza(void) {                         /* polecenie: dzwiek poz */
    for (int i = 0; i < 12; i++) {
        u32 t = zegar_ms();
        if (rodzaj == 2) kprintf("t=%u civ=%d picb=%d lvi=%d sr=%x poz=%u wpoz=%u\n", t, inb(nabm + 0x14) & 31, inw(nabm + 0x18), inb(nabm + 0x15), inw(nabm + 0x16), ac97_pozycja(), wpoz);
        else kprintf("t=%u lpib=%u wpoz=%u\n", t, hda_pozycja(), wpoz);
        u32 t0 = zegar_ms(); while (zegar_ms() - t0 < 25) audio_pompuj();
    }
}

void audio_glosnosc(int proc) { synteza_glosnosc(proc * 256 / 100); }
int  audio_glosnosc_pobierz(void) { return (synteza_glosnosc_pobierz() * 100 + 128) / 256; }
