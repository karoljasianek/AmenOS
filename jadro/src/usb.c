/* AmenOS - wspólna warstwa USB: wykrywanie kontrolerów, wyliczanie urządzeń, klawiatura i mysz HID.
 * Klawisze z klawiatury USB są zamieniane na kody PS/2 (zestaw 1) i trafiają do tej samej
 * kolejki co z klawiatury PS/2 - dzięki temu działa wszystko: AltGr, polskie litery, skróty. */
#include "usb.h"

/* ---------- pamięć dla kontrolerów (adres fizyczny = wirtualny) ---------- */

static u8 pula[768 * 1024] __attribute__((aligned(4096)));
static u32 pula_zajete;

void *usb_dma(u32 rozmiar) {
    rozmiar = (rozmiar + 4095u) & ~4095u;
    if (pula_zajete + rozmiar > sizeof pula) return 0;
    void *p = pula + pula_zajete;
    pula_zajete += rozmiar;
    memset(p, 0, rozmiar);
    return p;
}

/* ---------- kontrolery i urządzenia ---------- */

#define MAX_HC 8
#define MAX_UR 16
static usb_hc_t kontrolery[MAX_HC];
static int nkontr;
static usb_dev_t urzadzenia[MAX_UR];
static int usb_gotowe;

usb_hc_t *usb_nowy_kontroler(void) {
    if (nkontr == MAX_HC) return 0;
    usb_hc_t *h = &kontrolery[nkontr++];
    memset(h, 0, sizeof *h);
    return h;
}

usb_dev_t *usb_nowe(usb_hc_t *hc, int port, int predkosc) {
    for (int i = 0; i < MAX_UR; i++)
        if (!urzadzenia[i].jest) {
            usb_dev_t *d = &urzadzenia[i];
            memset(d, 0, sizeof *d);
            d->jest = 1; d->hc = hc; d->port = port; d->predkosc = predkosc;
            d->mps0 = predkosc == USB_SUPER ? 512 : predkosc == USB_HIGH ? 64 : 8;
            return d;
        }
    return 0;
}

void usb_usun(usb_dev_t *d) {
    if (!d->jest) return;
    for (int i = 0; i < MAX_UR; i++)                    /* najpierw urządzenia za tym koncentratorem */
        if (urzadzenia[i].jest && urzadzenia[i].rodzic == d) usb_usun(&urzadzenia[i]);
    if (d->hc->usun) d->hc->usun(d);
    d->jest = 0;
}

usb_dev_t *usb_lista(int *ile) { *ile = MAX_UR; return urzadzenia; }

void usb_odlacz_port(usb_hc_t *hc, int port) {
    for (int i = 0; i < MAX_UR; i++)
        if (urzadzenia[i].jest && urzadzenia[i].hc == hc && urzadzenia[i].port == port && !urzadzenia[i].rodzic)
            usb_usun(&urzadzenia[i]);
}

/* nowe urządzenie na porcie głównym albo porcie koncentratora: adres + konfiguracja */
void usb_podlaczono(usb_hc_t *hc, usb_dev_t *rodzic, int port_glowny, int port_rodzica, int predkosc) {
    usb_dev_t *d = usb_nowe(hc, port_glowny, predkosc);
    if (!d) return;
    d->rodzic = rodzic;
    d->port_rodzica = port_rodzica;
    if (rodzic) {
        d->poziom = rodzic->poziom + 1;
        d->trasa = rodzic->trasa | ((u32)(port_rodzica < 15 ? port_rodzica : 15) << (4 * rodzic->poziom));
    }
    if (!hc->adresuj(d)) { usb_usun(d); return; }
    usb_skonfiguruj(d);
}

/* ---------- koncentratory (hub) ---------- */

static int hub_status(usb_dev_t *h, int p, u16 *st, u16 *zm) {
    u8 b[4];
    if (usb_zadanie(h, 0xA3, 0, 0, (u16)p, b, 4) < 0) return 0;
    *st = (u16)(b[0] | b[1] << 8); *zm = (u16)(b[2] | b[3] << 8);
    return 1;
}

static void hub_port(usb_dev_t *h, int p) {
    u16 st, zm;
    if (!hub_status(h, p, &st, &zm)) return;
    if (zm & 1) usb_zadanie(h, 0x23, 1, 16, (u16)p, 0, 0);              /* wyczyść C_PORT_CONNECTION */
    usb_dev_t *dz = 0;
    for (int i = 0; i < MAX_UR; i++)
        if (urzadzenia[i].jest && urzadzenia[i].rodzic == h && urzadzenia[i].port_rodzica == p) dz = &urzadzenia[i];
    if (!(st & 1)) { if (dz) usb_usun(dz); return; }
    if (dz && !(zm & 1)) return;                                        /* bez zmian */
    if (dz) usb_usun(dz);
    usb_zadanie(h, 0x23, 3, 4, (u16)p, 0, 0);                           /* PORT_RESET */
    u32 t = zegar_ms();
    do { czekaj_ms(10); if (!hub_status(h, p, &st, &zm)) return; } while (!(zm & 0x10) && zegar_ms() - t < 500);
    usb_zadanie(h, 0x23, 1, 20, (u16)p, 0, 0);                          /* wyczyść C_PORT_RESET */
    if (!(st & 2)) return;
    int pred = h->predkosc == USB_SUPER ? USB_SUPER : (st & 0x200) ? USB_LOW : (st & 0x400) ? USB_HIGH : USB_FULL;
    czekaj_ms(10);
    usb_podlaczono(h->hc, h, h->port, p, pred);
}

static void hub_skonfiguruj(usb_dev_t *h) {
    u8 *b = usb_dma(4096);
    if (!b) return;
    if (usb_zadanie(h, 0xA0, 6, h->predkosc == USB_SUPER ? 0x2A00 : 0x2900, 0, b, 9) < 0) return;
    h->hub = 1;
    h->hub_porty = b[2] > 15 ? 15 : b[2];
    if (h->hc->hub) h->hc->hub(h);
    for (int p = 1; p <= h->hub_porty; p++) usb_zadanie(h, 0x23, 3, 8, (u16)p, 0, 0);   /* PORT_POWER */
    czekaj_ms((u32)b[5] * 2 + 50);
    for (int p = 1; p <= h->hub_porty; p++) hub_port(h, p);
}

const char *usb_predkosc_nazwa(int p) {
    static const char *n[] = { "niska (1,5 Mb/s)", "pełna (12 Mb/s)", "wysoka (480 Mb/s)", "SuperSpeed (5 Gb/s)" };
    return (p >= 0 && p <= 3) ? n[p] : "?";
}

int usb_zadanie(usb_dev_t *d, u8 typ, u8 req, u16 wartosc, u16 indeks, void *dane, u16 dl) {
    u8 s[8] = { typ, req, (u8)wartosc, (u8)(wartosc >> 8), (u8)indeks, (u8)(indeks >> 8), (u8)dl, (u8)(dl >> 8) };
    return d->hc->kontrolny(d, s, dane, dl);
}

/* Odczyt deskryptorów, wybór interfejsów HID (klawiatura/mysz w trybie boot), konfiguracja. */
int usb_skonfiguruj(usb_dev_t *d) {
    u8 *b = usb_dma(4096);                       /* pamięć jednorazowa - wystarczy na kilka urządzeń */
    if (!b) return 0;
    if (usb_zadanie(d, 0x80, 6, 0x0100, 0, b, 18) < 0) return 0;          /* deskryptor urządzenia */
    d->vid = (u16)(b[8] | b[9] << 8); d->pid = (u16)(b[10] | b[11] << 8);
    int klasa = b[4];
    if (usb_zadanie(d, 0x80, 6, 0x0200, 0, b, 9) < 0) return 0;           /* nagłówek konfiguracji */
    u16 cala = (u16)(b[2] | b[3] << 8);
    if (cala > 1024) cala = 1024;
    if (usb_zadanie(d, 0x80, 6, 0x0200, 0, b, cala) < 0) return 0;
    d->konfiguracja = b[5];

    int w_hid = 0, iface = 0, typ = 0;
    for (u32 i = 0; i + 2 <= cala && b[i] >= 2; i += b[i]) {
        u8 *x = b + i;
        if (x[1] == 4 && x[0] >= 9) {                                     /* interfejs */
            iface = x[2];
            w_hid = (x[5] == 3 && x[6] == 1 && (x[7] == 1 || x[7] == 2));
            typ = x[7] == 1 ? HID_KLAWIATURA : HID_MYSZ;
        } else if (x[1] == 5 && x[0] >= 7 && w_hid && d->nhid < 2) {      /* punkt końcowy */
            if ((x[2] & 0x80) && (x[3] & 3) == 3) {
                usb_hid_t *h = &d->hid[d->nhid++];
                h->typ = typ; h->iface = iface; h->ep = x[2] & 0x0F;
                h->mps = (x[4] | x[5] << 8) & 0x7FF;
                if (h->mps > 64) h->mps = 64;
                if (h->mps < 3) h->mps = 8;
                h->interval = x[6] ? x[6] : 10;
                h->bufor = usb_dma(64);
                w_hid = 0;
            }
        }
    }
    if (d->nhid && d->hc->konfiguruj && !d->hc->konfiguruj(d)) d->nhid = 0;
    if (usb_zadanie(d, 0x00, 9, d->konfiguracja, 0, 0, 0) < 0) return 0; /* SET_CONFIGURATION */
    if (klasa == 9 && d->poziom < 5) { hub_skonfiguruj(d); return 1; }
    for (int i = 0; i < d->nhid; i++) {
        usb_zadanie(d, 0x21, 0x0B, 0, (u16)d->hid[i].iface, 0, 0);        /* SET_PROTOCOL: boot */
        usb_zadanie(d, 0x21, 0x0A, 0, (u16)d->hid[i].iface, 0, 0);        /* SET_IDLE: tylko zmiany */
    }
    if (d->nhid && d->hc->start) d->hc->start(d);
    return 1;
}

/* ---------- klawiatura HID -> kody PS/2 (zestaw 1); 0x100 = prefiks E0 ---------- */

static const u16 hid_ps2[0x68] = {
    0, 0, 0, 0, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,        /* 00-0F: a-l */
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,        /* 10-1D: m-z */
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,                                /* 1E-27: 1-0 */
    0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29,        /* 28-35 */
    0x33, 0x34, 0x35, 0x3A,                                                                    /* 36-39 */
    0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,                    /* 3A-45: F1-F12 */
    0x137, 0x46, 0, 0x152, 0x147, 0x149, 0x153, 0x14F, 0x151, 0x14D, 0x14B, 0x150, 0x148,      /* 46-52 */
    0x45, 0x135, 0x37, 0x4A, 0x4E, 0x11C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48,      /* 53-60 */
    0x49, 0x52, 0x53, 0x56, 0, 0, 0                                                             /* 61-67 */
};
static const u16 mod_ps2[8] = { 0x1D, 0x2A, 0x38, 0x15B, 0x11D, 0x36, 0x138, 0x15C };

static void wloz(u16 kod, int puszczony) {
    if (!kod) return;
    if (kod & 0x100) kbd_wloz_kod(0xE0);
    kbd_wloz_kod((u8)((kod & 0x7F) | (puszczony ? 0x80 : 0)));
}

static u16 powt_kod;
static u32 powt_od, powt_ost;

static void raport_klawiatury(usb_dev_t *d, const u8 *r) {
    if (r[2] == 1) return;                                      /* za dużo klawiszy naraz */
    u8 zm = (u8)(r[0] ^ d->stare[0]);
    for (int b = 0; b < 8; b++) if (zm & (1 << b)) wloz(mod_ps2[b], !(r[0] & (1 << b)));
    for (int i = 2; i < 8; i++) {                               /* puszczone */
        u8 u = d->stare[i];
        if (u < 4) continue;
        int jest = 0;
        for (int j = 2; j < 8; j++) if (r[j] == u) jest = 1;
        if (!jest && u < 0x68) { wloz(hid_ps2[u], 1); if (powt_kod == hid_ps2[u]) powt_kod = 0; }
    }
    for (int i = 2; i < 8; i++) {                               /* wciśnięte */
        u8 u = r[i];
        if (u < 4 || u >= 0x68) continue;
        int bylo = 0;
        for (int j = 2; j < 8; j++) if (d->stare[j] == u) bylo = 1;
        if (!bylo) { wloz(hid_ps2[u], 0); powt_kod = hid_ps2[u]; powt_od = powt_ost = zegar_ms(); }
    }
    memcpy(d->stare, r, 8);
}

void usb_raport(usb_dev_t *d, int i, const u8 *r, int dl) {
    if (d->hid[i].typ == HID_KLAWIATURA && dl >= 8) raport_klawiatury(d, r);
    else if (d->hid[i].typ == HID_MYSZ && dl >= 3) {
        int dx = (signed char)r[1], dy = (signed char)r[2];
        if (gui_aktywne) gui_mysz(dx, dy, r[0] & 7);
    }
}

/* ---------- przejęcie EHCI od BIOS-u: porty wracają do kontrolerów UHCI ---------- */

static void ehci_oddaj_porty(int bus, int dev, int fn) {
    u32 bar = pci_czytaj(bus, dev, fn, 0x10) & ~0xFu;
    if (!bar) return;
    pci_pisz(bus, dev, fn, 0x04, pci_czytaj(bus, dev, fn, 0x04) | 0x06);
    volatile u8 *cap = (volatile u8 *)bar;
    u32 hcc = *(volatile u32 *)(cap + 8);
    int eecp = (hcc >> 8) & 0xFF;
    for (int n = 0; eecp >= 0x40 && n < 8; n++) {
        u32 v = pci_czytaj(bus, dev, fn, eecp);
        if ((v & 0xFF) == 1) {                                  /* USBLEGSUP */
            pci_pisz(bus, dev, fn, eecp, v | (1u << 24));
            u32 t = zegar_ms();
            while ((pci_czytaj(bus, dev, fn, eecp) & (1u << 16)) && zegar_ms() - t < 1000) ;
            pci_pisz(bus, dev, fn, eecp + 4, 0);                /* wyłącz SMI */
            break;
        }
        eecp = (v >> 8) & 0xFF;
    }
    volatile u32 *op = (volatile u32 *)(cap + cap[0]);
    op[0] &= ~1u;                                               /* stop */
    u32 t = zegar_ms();
    while (!(op[1] & (1u << 12)) && zegar_ms() - t < 50) ;
    op[0] |= 2;                                                 /* reset: CONFIGFLAG = 0 */
    t = zegar_ms();
    while ((op[0] & 2) && zegar_ms() - t < 100) ;
    op[0x40 / 4] = 0;                                           /* porty do kontrolerów towarzyszących */
}

/* ---------- start i odpytywanie ---------- */

void usb_init(void) {
    /* najpierw EHCI (oddanie portów), potem xHCI i UHCI */
    for (int etap = 0; etap < 2; etap++)
        for (int b = 0; b < 256; b++)
            for (int dv = 0; dv < 32; dv++)
                for (int f = 0; f < 8; f++) {
                    u32 id = pci_czytaj(b, dv, f, 0);
                    if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
                    u32 kl = pci_czytaj(b, dv, f, 8);
                    if ((kl >> 16) == 0x0C03) {
                        u8 prog = (u8)(kl >> 8);
                        if (etap == 0 && prog == 0x20) ehci_oddaj_porty(b, dv, f);
                        if (etap == 1 && prog == 0x30) xhci_init(b, dv, f);
                        if (etap == 1 && prog == 0x00) uhci_init(b, dv, f);
                    }
                    if (f == 0 && !(pci_czytaj(b, dv, 0, 0x0C) & 0x00800000)) break;   /* jednofunkcyjne */
                }
    usb_gotowe = 1;
}

void usb_odpytaj(void) {
    static int w_trakcie;
    if (!usb_gotowe || w_trakcie) return;
    w_trakcie = 1;
    for (int i = 0; i < nkontr; i++) if (kontrolery[i].odpytaj) kontrolery[i].odpytaj(&kontrolery[i]);
    static u32 ost_hub;                                         /* porty koncentratorów: co sekundę */
    if (zegar_ms() - ost_hub > 1000) {
        ost_hub = zegar_ms();
        for (int i = 0; i < MAX_UR; i++)
            if (urzadzenia[i].jest && urzadzenia[i].hub)
                for (int p = 1; p <= urzadzenia[i].hub_porty && urzadzenia[i].jest; p++) hub_port(&urzadzenia[i], p);
    }
    if (powt_kod) {                                             /* powtarzanie przytrzymanego klawisza */
        u32 t = zegar_ms();
        if (t - powt_od > 500 && t - powt_ost >= 33) { powt_ost = t; wloz(powt_kod, 0); }
    }
    w_trakcie = 0;
}

int usb_wylaczone;

void cmd_usb(void) {
    vga_color(ZOLTY, CZARNY); kprintf("\n  USB\n");
    vga_color(SZARY, CZARNY);
    if (usb_wylaczone) {
        kprintf("  Sterowniki USB są wyłączone (pozycja \"bez sterowników USB\" w menu startowym).\n"
                "  Klawiatura i mysz działają przez emulację PS/2 w BIOS-ie.\n\n");
        return;
    }
    if (!nkontr) kprintf("  Nie znaleziono obsługiwanego kontrolera USB (UHCI lub xHCI).\n");
    for (int i = 0; i < nkontr; i++)
        kprintf("  Kontroler %d: %s (%04x:%04x), portów: %d\n", i + 1, kontrolery[i].nazwa, kontrolery[i].vid,
                kontrolery[i].did, kontrolery[i].nportow);
    int n = 0;
    for (int i = 0; i < MAX_UR; i++) {
        usb_dev_t *d = &urzadzenia[i];
        if (!d->jest) continue;
        n++;
        const char *co = d->hub ? "koncentrator USB" : d->nhid == 0 ? "urządzenie (nieobsługiwane)" :
                         d->nhid == 2 ? "klawiatura + mysz" :
                         d->hid[0].typ == HID_KLAWIATURA ? "klawiatura" : "mysz";
        vga_color(BIALY, CZARNY);
        kprintf("  ");
        for (int w = 0; w < d->poziom; w++) kprintf("  ");
        kprintf("- %s", co);
        vga_color(SZARY, CZARNY);
        if (d->rodzic) kprintf("  %04x:%04x, port koncentratora %d, %s\n", d->vid, d->pid, d->port_rodzica, usb_predkosc_nazwa(d->predkosc));
        else kprintf("  %04x:%04x, kontroler %d, port %d, %s\n", d->vid, d->pid,
                     (int)(d->hc - kontrolery) + 1, d->port, usb_predkosc_nazwa(d->predkosc));
    }
    if (!n && nkontr) kprintf("  Brak podłączonych urządzeń.\n");
    kprintf("\n");
}
