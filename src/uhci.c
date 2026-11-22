/* AmenOS - sterownik kontrolera USB UHCI (USB 1.1, starsze komputery z chipsetami Intel/VIA)
 * Lista 1024 ramek wskazuje łańcuch kolejek: urządzenia HID (co 1 ms) -> transfery kontrolne. */
#include "usb.h"

typedef struct { volatile u32 link, ctrl, token, bufor; u32 rez[4]; } td_t;    /* 32 bajty, wyrównane do 16 */
typedef struct { volatile u32 glowa, element; u32 rez[2]; } qh_t;

#define TD_AKTYWNY   (1u << 23)
#define TD_BLAD      0x00760000u            /* stall, bufor, babble, CRC/timeout, bitstuff (bez NAK) */
#define TD_LS        (1u << 26)
#define TD_IOC       (1u << 24)
#define TD_CERR      (3u << 27)
#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1

typedef struct {
    usb_hc_t *hc;
    u16 io;
    u32 *ramki;
    qh_t *qh_kotwica, *qh_ster;
    td_t *td;                               /* pula TD dla transferów kontrolnych */
    int nastepny_adres;
    u32 ost_spr;
} uhci_t;

static uhci_t uk[8];
static int nuk;

static u16 portrej(uhci_t *u, int p) { return (u16)(u->io + 0x10 + 2 * (p - 1)); }

/* ---------- transfer kontrolny ---------- */

static int u_kontrolny(usb_dev_t *d, const u8 *s, void *dane, int dl) {
    uhci_t *u = d->hc->dane;
    static u8 setup_buf[8] __attribute__((aligned(16)));
    memcpy(setup_buf, s, 8);
    u32 ls = d->predkosc == USB_LOW ? TD_LS : 0;
    u32 adr = ((u32)d->adres << 8);
    int we = s[0] & 0x80, n = 0;
    td_t *t = u->td;
    t[n].ctrl = TD_CERR | ls | TD_AKTYWNY;
    t[n].token = (7u << 21) | adr | PID_SETUP;
    t[n].bufor = (u32)setup_buf;
    n++;
    int toggle = 1;
    for (int poz = 0; poz < dl && n < 60; poz += d->mps0) {
        int kaw = dl - poz < d->mps0 ? dl - poz : d->mps0;
        t[n].ctrl = TD_CERR | ls | TD_AKTYWNY | (we ? (1u << 29) : 0);
        t[n].token = ((u32)(kaw - 1) << 21) | ((u32)toggle << 19) | adr | (we ? PID_IN : PID_OUT);
        t[n].bufor = (u32)dane + (u32)poz;
        toggle ^= 1; n++;
    }
    t[n].ctrl = TD_CERR | ls | TD_AKTYWNY | TD_IOC;
    t[n].token = (0x7FFu << 21) | (1u << 19) | adr | ((we && dl) ? PID_OUT : PID_IN);
    t[n].bufor = 0;
    n++;
    for (int i = 0; i < n; i++) t[i].link = (i == n - 1) ? 1u : ((u32)&t[i + 1] | 4u);
    BARIERA();
    u->qh_ster->element = (u32)&t[0];
    u32 t0 = zegar_ms();
    int wynik = -1;
    while (zegar_ms() - t0 < 1000) {
        int i = 0;
        while (i < n && !(t[i].ctrl & TD_AKTYWNY)) i++;
        int blad = 0;
        for (int j = 0; j < i; j++) if (t[j].ctrl & TD_BLAD) blad = 1;
        if (blad) break;
        if (i == n) { wynik = dl; break; }
        /* krótki pakiet danych IN: przeskocz od razu do etapu statusu */
        if (we && i >= 2 && i < n - 1) {
            u32 pc = t[i - 1].ctrl;
            u32 maks = ((t[i - 1].token >> 21) + 1) & 0x7FF, jest = ((pc & 0x7FF) + 1) & 0x7FF;
            if (jest < maks) {
                for (int j = i; j < n - 1; j++) t[j].ctrl &= ~TD_AKTYWNY;
                u->qh_ster->element = (u32)&t[n - 1];
            }
        }
    }
    u->qh_ster->element = 1;
    return wynik;
}

/* ---------- przerwania HID ---------- */

static void uzbroj(usb_dev_t *d, usb_hid_t *h) {
    td_t *t = h->td;
    qh_t *q = h->qh;
    t->link = 1;
    t->bufor = (u32)h->bufor;
    t->token = ((u32)(h->mps - 1) << 21) | ((u32)h->toggle << 19) | ((u32)h->ep << 15) | ((u32)d->adres << 8) | PID_IN;
    BARIERA();
    t->ctrl = TD_CERR | (d->predkosc == USB_LOW ? TD_LS : 0) | TD_AKTYWNY | (1u << 29);
    BARIERA();
    q->element = (u32)t;
}

static void u_start(usb_dev_t *d) {
    uhci_t *u = d->hc->dane;
    for (int k = 0; k < d->nhid; k++) {
        usb_hid_t *h = &d->hid[k];
        h->td = usb_dma(64);
        h->qh = (u8 *)h->td + 32;
        h->toggle = 0;
        qh_t *q = h->qh;
        q->element = 1;
        q->glowa = u->qh_kotwica->glowa;                 /* wstaw na początek łańcucha */
        uzbroj(d, h);
        BARIERA();
        u->qh_kotwica->glowa = (u32)q | 2;
    }
}

/* ---------- porty ---------- */

static void port_reset_i_wylicz(uhci_t *u, int p) {
    u16 r = portrej(u, p);
    outw(r, (u16)(inw(r) | 0x200));
    czekaj_ms(58);
    outw(r, (u16)(inw(r) & ~0x200));
    czekaj_ms(5);
    u16 s = 0;
    for (int i = 0; i < 16; i++) {
        s = inw(r);
        if (!(s & 1)) return;                            /* nic nie podłączono */
        if (s & 0x0A) { outw(r, (u16)((s & ~0x0Eu) | 0x0A)); continue; }   /* wyczyść zmiany */
        if (s & 4) break;                                /* włączony */
        outw(r, (u16)((s & ~0x0Au) | 4));
        czekaj_ms(10);
    }
    if (!(inw(r) & 4)) return;
    czekaj_ms(10);
    usb_podlaczono(u->hc, 0, p, 0, (s & 0x100) ? USB_LOW : USB_FULL);
}

/* adres 0 -> rozmiar pakietu EP0 -> SET_ADDRESS (także za koncentratorem) */
static int u_adresuj(usb_dev_t *d) {
    uhci_t *u = d->hc->dane;
    d->adres = 0;
    u8 *b = usb_dma(4096);
    if (!b || usb_zadanie(d, 0x80, 6, 0x0100, 0, b, 8) < 0) return 0;
    if (b[7]) d->mps0 = b[7];
    int adr = u->nastepny_adres++;
    if (adr > 127) return 0;
    if (usb_zadanie(d, 0x00, 5, (u16)adr, 0, 0, 0) < 0) return 0;      /* SET_ADDRESS */
    d->adres = adr;
    czekaj_ms(10);
    return 1;
}

static void u_usun(usb_dev_t *d) {
    uhci_t *u = d->hc->dane;
    for (int k = 0; k < d->nhid; k++) {                   /* wyjmij kolejki HID z łańcucha */
        qh_t *q = d->hid[k].qh;
        if (!q) continue;
        q->element = 1;
        qh_t *p = u->qh_kotwica;
        while ((p->glowa & ~0xFu) && (p->glowa & ~0xFu) != (u32)q && (p->glowa & 2)) p = (qh_t *)(p->glowa & ~0xFu);
        if ((p->glowa & ~0xFu) == (u32)q) p->glowa = q->glowa;
        d->hid[k].qh = 0; d->hid[k].td = 0;
    }
}

static void u_odpytaj(usb_hc_t *hc) {
    uhci_t *u = hc->dane;
    int ile;
    usb_dev_t *lista = usb_lista(&ile);
    for (int i = 0; i < ile; i++) {                       /* raporty klawiatur i myszy */
        usb_dev_t *d = &lista[i];
        if (!d->jest || d->hc != hc) continue;
        for (int k = 0; k < d->nhid; k++) {
            usb_hid_t *h = &d->hid[k];
            td_t *t = h->td;
            if (!t) continue;
            u32 c = t->ctrl;
            if (c & TD_AKTYWNY) continue;
            if (!(c & TD_BLAD)) {
                usb_raport(d, k, h->bufor, (int)(((c & 0x7FF) + 1) & 0x7FF));
                h->toggle ^= 1;
            }
            uzbroj(d, h);
        }
    }
    if (zegar_ms() - u->ost_spr < 500) return;           /* podłączenia sprawdzamy co pół sekundy */
    u->ost_spr = zegar_ms();
    for (int p = 1; p <= hc->nportow; p++) {
        u16 r = portrej(u, p), s = inw(r);
        if (!(s & 2)) continue;                           /* brak zmiany podłączenia */
        outw(r, (u16)((s & ~0x0Eu) | 0x02));
        usb_odlacz_port(hc, p);
        if (s & 1) port_reset_i_wylicz(u, p);
    }
}

/* ---------- start kontrolera ---------- */

int uhci_init(int bus, int dev, int fn) {
    if (nuk == 8) return 0;
    u32 bar = pci_czytaj(bus, dev, fn, 0x20);
    if (!(bar & 1)) return 0;
    uhci_t *u = &uk[nuk];
    memset(u, 0, sizeof *u);
    u->io = (u16)(bar & ~3u);
    u32 id = pci_czytaj(bus, dev, fn, 0);
    pci_pisz(bus, dev, fn, 0x04, pci_czytaj(bus, dev, fn, 0x04) | 0x05);    /* porty I/O + bus master */
    u32 leg = pci_czytaj(bus, dev, fn, 0xC0);
    pci_pisz(bus, dev, fn, 0xC0, (leg & 0xFFFF0000u) | 0x8F00);            /* koniec emulacji PS/2 przez BIOS */

    outw(u->io, 4); czekaj_ms(20); outw(u->io, 0);                         /* reset globalny */
    outw(u->io, 2);
    u32 t = zegar_ms();
    while ((inw(u->io) & 2) && zegar_ms() - t < 100) ;
    outw((u16)(u->io + 4), 0);                                             /* bez przerwań */
    outw((u16)(u->io + 2), 0xFFFF);

    u->ramki = usb_dma(4096);
    u8 *m = usb_dma(4096);
    u->td = usb_dma(64 * sizeof(td_t));
    if (!u->ramki || !m || !u->td) return 0;
    u->qh_kotwica = (qh_t *)m;
    u->qh_ster = (qh_t *)(m + 32);
    u->qh_ster->glowa = 1; u->qh_ster->element = 1;
    u->qh_kotwica->glowa = (u32)u->qh_ster | 2; u->qh_kotwica->element = 1;
    for (int i = 0; i < 1024; i++) u->ramki[i] = (u32)u->qh_kotwica | 2;
    outl((u16)(u->io + 8), (u32)u->ramki);
    outw((u16)(u->io + 6), 0);
    outb((u16)(u->io + 0x0C), 0x40);
    outw(u->io, 0x81);                                                     /* start, pakiety do 64 B */
    u->nastepny_adres = 1;

    usb_hc_t *hc = usb_nowy_kontroler();
    if (!hc) return 0;
    nuk++;
    u->hc = hc;
    hc->nazwa = "UHCI (USB 1.1)";
    hc->vid = (u16)id; hc->did = (u16)(id >> 16);
    hc->dane = u;
    hc->kontrolny = u_kontrolny;
    hc->start = u_start;
    hc->odpytaj = u_odpytaj;
    hc->adresuj = u_adresuj;
    hc->usun = u_usun;
    int n = 0;
    while (n < 8) { u16 s = inw(portrej(u, n + 1)); if (s == 0xFFFF || !(s & 0x80)) break; n++; }
    hc->nportow = n ? n : 2;
    for (int p = 1; p <= hc->nportow; p++) {
        u16 r = portrej(u, p), s = inw(r);
        if (s & 1) { outw(r, (u16)((s & ~0x0Eu) | 0x0A)); port_reset_i_wylicz(u, p); }
    }
    u->ost_spr = zegar_ms();
    return 1;
}
