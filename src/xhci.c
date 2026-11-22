/* AmenOS - sterownik kontrolera USB xHCI (USB 3.x, komputery od ok. 2012 r.)
 * Odpytywanie bez przerwań: pierścień zdarzeń sprawdzany przy każdym odczycie klawiatury. */
#include "usb.h"

#define TRB_NORMAL   1
#define TRB_SETUP    2
#define TRB_DATA     3
#define TRB_STATUS   4
#define TRB_LINK     6
#define TRB_EN_SLOT  9
#define TRB_ADRES    11
#define TRB_KONF_EP  12
#define TRB_OCEN_KTX 13
#define EV_TRANSFER  32
#define EV_KOMENDA   33
#define EV_PORT      34

#define RING_N 64
#define EV_N   128
#define MAX_SLOT 16

typedef struct { volatile u32 p0, p1, st, ctl; } trb_t;
typedef struct { trb_t *trb; int idx, cykl; } ring_t;

typedef struct {
    usb_dev_t *dev;
    ring_t ep0, ep[2];
    u32 *wej, *wyj;                 /* kontekst wejściowy i urządzenia */
    volatile int gotowe, kod;
} slot_t;

typedef struct {
    usb_hc_t *hc;
    volatile u8 *cap;
    volatile u32 *op, *ir, *db;
    int maks_slot, csz;
    u32 *dcbaa;
    ring_t kom;
    trb_t *ev;
    int ev_idx, ev_cykl;
    volatile int kom_gotowe, kom_kod, kom_slot;
    slot_t slot[MAX_SLOT + 1];
    u32 port_zmiana;                /* porty ze zgłoszoną zmianą stanu */
    u32 ost_spr;
} xhci_t;

static xhci_t xk[4];
static int nxk;

/* ---------- pierścienie TRB ---------- */

static int ring_init(ring_t *r) {
    r->trb = usb_dma(RING_N * 16);
    if (!r->trb) return 0;
    r->idx = 0; r->cykl = 1;
    trb_t *l = &r->trb[RING_N - 1];
    l->p0 = (u32)r->trb; l->p1 = 0; l->st = 0;
    l->ctl = (TRB_LINK << 10) | (1 << 1);          /* przełącz cykl */
    return 1;
}

static trb_t *ring_wstaw(ring_t *r, u32 p0, u32 p1, u32 st, u32 ctl) {
    trb_t *t = &r->trb[r->idx];
    t->p0 = p0; t->p1 = p1; t->st = st;
    BARIERA();
    t->ctl = (ctl & ~1u) | (u32)r->cykl;
    if (++r->idx == RING_N - 1) {
        trb_t *l = &r->trb[RING_N - 1];
        BARIERA();
        l->ctl = (TRB_LINK << 10) | (1 << 1) | (u32)r->cykl;
        r->idx = 0;
        r->cykl ^= 1;
    }
    return t;
}

/* ---------- zdarzenia ---------- */

static void wznow_przerwanie(xhci_t *x, int s, int k);

static void zdarzenia(xhci_t *x) {
    int bylo = 0;
    for (int n = 0; n < EV_N; n++) {
        trb_t *e = &x->ev[x->ev_idx];
        if ((int)(e->ctl & 1) != x->ev_cykl) break;
        u32 typ = (e->ctl >> 10) & 63, kod = e->st >> 24, s = e->ctl >> 24;
        if (typ == EV_KOMENDA) { x->kom_kod = (int)kod; x->kom_slot = (int)s; x->kom_gotowe = 1; }
        else if (typ == EV_PORT) { int p = (int)(e->p0 >> 24); if (p > 0 && p <= 32) x->port_zmiana |= 1u << (p - 1); }
        else if (typ == EV_TRANSFER && s >= 1 && s <= MAX_SLOT) {
            slot_t *sl = &x->slot[s];
            u32 dci = (e->ctl >> 16) & 31;
            if (dci == 1) { sl->kod = (int)kod; sl->gotowe = 1; }
            else if (sl->dev) {
                for (int k = 0; k < sl->dev->nhid; k++) {
                    usb_hid_t *h = &sl->dev->hid[k];
                    if (h->dci != (int)dci) continue;
                    if (kod == 1 || kod == 13) usb_raport(sl->dev, k, h->bufor, h->mps - (int)(e->st & 0xFFFFFF));
                    wznow_przerwanie(x, (int)s, k);
                }
            }
        }
        if (++x->ev_idx == EV_N) { x->ev_idx = 0; x->ev_cykl ^= 1; }
        bylo = 1;
    }
    if (bylo) {
        x->ir[6] = (u32)&x->ev[x->ev_idx] | (1u << 3);   /* ERDP + wyczyść EHB */
        x->ir[7] = 0;
    }
}

static int komenda(xhci_t *x, u32 p0, u32 p1, u32 st, u32 ctl) {
    x->kom_gotowe = 0;
    ring_wstaw(&x->kom, p0, p1, st, ctl);
    x->db[0] = 0;
    u32 t = zegar_ms();
    while (!x->kom_gotowe && zegar_ms() - t < 1000) zdarzenia(x);
    return x->kom_gotowe ? x->kom_kod : -1;
}

/* ---------- transfery ---------- */

static int x_kontrolny(usb_dev_t *d, const u8 *s, void *dane, int dl) {
    xhci_t *x = d->hc->dane;
    slot_t *sl = &x->slot[d->slot];
    int we = s[0] & 0x80;
    u32 trt = dl ? (we ? 3u : 2u) : 0u;
    ring_wstaw(&sl->ep0, (u32)(s[0] | s[1] << 8 | s[2] << 16 | (u32)s[3] << 24),
               (u32)(s[4] | s[5] << 8 | s[6] << 16 | (u32)s[7] << 24), 8, (TRB_SETUP << 10) | (1 << 6) | (trt << 16));
    if (dl) ring_wstaw(&sl->ep0, (u32)dane, 0, (u32)dl, (TRB_DATA << 10) | (we ? 1u << 16 : 0));
    sl->gotowe = 0;
    ring_wstaw(&sl->ep0, 0, 0, 0, (TRB_STATUS << 10) | ((dl && we) ? 0 : 1u << 16) | (1 << 5));
    x->db[d->slot] = 1;
    u32 t = zegar_ms();
    while (!sl->gotowe && zegar_ms() - t < 1000) zdarzenia(x);
    if (!sl->gotowe || (sl->kod != 1 && sl->kod != 13)) return -1;
    return dl;
}

static void wznow_przerwanie(xhci_t *x, int s, int k) {
    slot_t *sl = &x->slot[s];
    usb_hid_t *h = &sl->dev->hid[k];
    ring_wstaw(&sl->ep[k], (u32)h->bufor, 0, (u32)h->mps, (TRB_NORMAL << 10) | (1 << 5) | (1 << 2));
    x->db[s] = (u32)h->dci;
}

static u32 *ktx(xhci_t *x, u32 *baza, int i) { return (u32 *)((u8 *)baza + i * x->csz); }

static int x_konfiguruj(usb_dev_t *d) {
    xhci_t *x = d->hc->dane;
    slot_t *sl = &x->slot[d->slot];
    memset(sl->wej, 0, 4096);
    u32 *icc = ktx(x, sl->wej, 0);
    int maks_dci = 1;
    icc[1] = 1;
    for (int k = 0; k < d->nhid; k++) {
        usb_hid_t *h = &d->hid[k];
        h->dci = h->ep * 2 + 1;
        if (h->dci > maks_dci) maks_dci = h->dci;
        if (!ring_init(&sl->ep[k])) return 0;
        icc[1] |= 1u << h->dci;
        int odstep;                                     /* 2^odstep x 125 us */
        if (d->predkosc >= USB_HIGH) odstep = h->interval - 1;
        else { int m = h->interval * 8; odstep = 0; while ((2 << odstep) <= m) odstep++; }
        if (odstep < 3) odstep = 3;
        if (odstep > 10) odstep = 10;
        u32 *ep = ktx(x, sl->wej, h->dci + 1);
        ep[0] = (u32)odstep << 16;
        ep[1] = (3u << 1) | (7u << 3) | ((u32)h->mps << 16);   /* przerwaniowy IN */
        ep[2] = (u32)sl->ep[k].trb | 1;
        ep[3] = 0;
        ep[4] = (u32)h->mps | ((u32)h->mps << 16);
    }
    u32 *sk = ktx(x, sl->wej, 1), *wy = ktx(x, sl->wyj, 0);
    sk[0] = (wy[0] & ~(31u << 27)) | ((u32)maks_dci << 27);
    sk[1] = wy[1]; sk[2] = wy[2]; sk[3] = 0;
    return komenda(x, (u32)sl->wej, 0, 0, (TRB_KONF_EP << 10) | ((u32)d->slot << 24)) == 1;
}

static void x_start(usb_dev_t *d) {
    xhci_t *x = d->hc->dane;
    for (int k = 0; k < d->nhid; k++) wznow_przerwanie(x, d->slot, k);
}

/* ---------- porty i nowe urządzenia ---------- */

static volatile u32 *portsc(xhci_t *x, int p) { return x->op + (0x400 + 0x10 * (p - 1)) / 4; }
#define PORT_NEUTRALNIE(v) ((v) & ~(0x02u | 0x10u | 0x00FE0000u))

/* nadanie adresu: slot, kontekst z trasą przez koncentratory, Address Device */
static int x_adresuj(usb_dev_t *d) {
    xhci_t *x = d->hc->dane;
    if (komenda(x, 0, 0, 0, TRB_EN_SLOT << 10) != 1) return 0;
    int s = x->kom_slot;
    if (s < 1 || s > MAX_SLOT) return 0;
    slot_t *sl = &x->slot[s];
    d->slot = s;
    if (!sl->wej) { sl->wej = usb_dma(4096); sl->wyj = usb_dma(4096); }
    else { memset(sl->wej, 0, 4096); memset(sl->wyj, 0, 4096); }
    if (!sl->wej || !sl->wyj || !ring_init(&sl->ep0)) return 0;
    x->dcbaa[2 * s] = (u32)sl->wyj; x->dcbaa[2 * s + 1] = 0;
    static const u32 kod_pred[4] = { 2, 1, 3, 4 };
    u32 sp = kod_pred[d->predkosc];
    u32 *icc = ktx(x, sl->wej, 0), *sk = ktx(x, sl->wej, 1), *e0 = ktx(x, sl->wej, 2);
    icc[1] = 3;
    sk[0] = (1u << 27) | (sp << 20) | (d->trasa & 0xFFFFF);
    sk[1] = (u32)d->port << 16;
    usb_dev_t *r = d->rodzic;
    if (r && r->predkosc == USB_HIGH && d->predkosc <= USB_FULL)
        sk[2] = (u32)r->slot | ((u32)d->port_rodzica << 8);          /* tłumacz transakcji (TT) koncentratora */
    e0[1] = (3u << 1) | (4u << 3) | ((u32)d->mps0 << 16);
    e0[2] = (u32)sl->ep0.trb | 1;
    e0[4] = 8;
    sl->dev = d;
    if (komenda(x, (u32)sl->wej, 0, 0, (TRB_ADRES << 10) | ((u32)s << 24)) != 1) { sl->dev = 0; return 0; }
    czekaj_ms(10);
    /* prawdziwy rozmiar pakietu EP0 (dla pełnej prędkości bywa 8..64) */
    u8 *b = usb_dma(4096);
    if (b && usb_zadanie(d, 0x80, 6, 0x0100, 0, b, 8) >= 0 && b[7] && b[7] != d->mps0 && d->predkosc <= USB_FULL) {
        d->mps0 = b[7];
        memset(sl->wej, 0, 4096);
        icc[1] = 2;
        e0[1] = (3u << 1) | (4u << 3) | ((u32)d->mps0 << 16);
        komenda(x, (u32)sl->wej, 0, 0, (TRB_OCEN_KTX << 10) | ((u32)s << 24));
    }
    return 1;
}

/* koncentrator: ustaw w kontekście slotu pola Hub i liczbę portów */
static void x_hub(usb_dev_t *d) {
    xhci_t *x = d->hc->dane;
    slot_t *sl = &x->slot[d->slot];
    memset(sl->wej, 0, 4096);
    u32 *icc = ktx(x, sl->wej, 0), *sk = ktx(x, sl->wej, 1), *wy = ktx(x, sl->wyj, 0);
    icc[1] = 1;
    sk[0] = wy[0] | (1u << 26);
    sk[1] = (wy[1] & 0x00FFFFFFu) | ((u32)d->hub_porty << 24);
    sk[2] = wy[2];
    komenda(x, (u32)sl->wej, 0, 0, (TRB_KONF_EP << 10) | ((u32)d->slot << 24));
}

static void x_usun(usb_dev_t *d) {
    xhci_t *x = d->hc->dane;
    if (d->slot < 1 || d->slot > MAX_SLOT || x->slot[d->slot].dev != d) return;
    x->slot[d->slot].dev = 0;
    komenda(x, 0, 0, 0, (10u << 10) | ((u32)d->slot << 24));        /* wyłącz slot */
}

static void port_urzadzenie(xhci_t *x, int p) {
    volatile u32 *ps = portsc(x, p);
    u32 v = *ps;
    if (!(v & 1)) { usb_odlacz_port(x->hc, p); return; }
    for (int s = 1; s <= MAX_SLOT; s++)
        if (x->slot[s].dev && x->slot[s].dev->port == p && !x->slot[s].dev->rodzic) return;   /* już jest */
    if (!(v & 2)) {                                       /* port USB 2: reset */
        *ps = PORT_NEUTRALNIE(v) | (1u << 4);
        u32 t = zegar_ms();
        while (zegar_ms() - t < 300 && !(*ps & (1u << 21))) ;
        czekaj_ms(20);
    }
    v = *ps;
    *ps = PORT_NEUTRALNIE(v) | (v & 0x00FE0000u);         /* wyczyść bity zmian */
    if (!(v & 2) || !(v & 1)) return;
    int sp = (v >> 10) & 15;
    usb_podlaczono(x->hc, 0, p, 0, sp == 2 ? USB_LOW : sp == 1 ? USB_FULL : sp == 3 ? USB_HIGH : USB_SUPER);
}

static void x_odpytaj(usb_hc_t *hc) {
    xhci_t *x = hc->dane;
    zdarzenia(x);
    if (x->port_zmiana && zegar_ms() - x->ost_spr > 200) {
        x->ost_spr = zegar_ms();
        u32 zm = x->port_zmiana;
        x->port_zmiana = 0;
        for (int p = 1; p <= hc->nportow && p <= 32; p++) if (zm & (1u << (p - 1))) port_urzadzenie(x, p);
    }
}

/* ---------- start kontrolera ---------- */

int xhci_init(int bus, int dev, int fn) {
    if (nxk == 4) return 0;
    u32 bar = pci_czytaj(bus, dev, fn, 0x10);
    if ((bar & 6) == 4 && pci_czytaj(bus, dev, fn, 0x14)) return 0;   /* powyżej 4 GB - nieobsługiwane */
    bar &= ~0xFu;
    if (!bar) return 0;
    u32 id = pci_czytaj(bus, dev, fn, 0);
    pci_pisz(bus, dev, fn, 0x04, pci_czytaj(bus, dev, fn, 0x04) | 0x06);
    if ((id & 0xFFFF) == 0x8086) {                       /* Intel: przełącz porty z EHCI na xHCI */
        pci_pisz(bus, dev, fn, 0xD8, pci_czytaj(bus, dev, fn, 0xDC));
        pci_pisz(bus, dev, fn, 0xD0, pci_czytaj(bus, dev, fn, 0xD4));
    }
    xhci_t *x = &xk[nxk];
    memset(x, 0, sizeof *x);
    x->cap = (volatile u8 *)bar;
    u32 hcs1 = *(volatile u32 *)(x->cap + 4), hcs2 = *(volatile u32 *)(x->cap + 8), hcc = *(volatile u32 *)(x->cap + 0x10);
    x->op = (volatile u32 *)(x->cap + x->cap[0]);
    x->ir = (volatile u32 *)(x->cap + (*(volatile u32 *)(x->cap + 0x18) & ~0x1Fu) + 0x20);
    x->db = (volatile u32 *)(x->cap + (*(volatile u32 *)(x->cap + 0x14) & ~3u));
    x->csz = (hcc & 4) ? 64 : 32;

    /* przejęcie od BIOS-u (USB Legacy Support) */
    u32 xecp = (hcc >> 16) << 2;
    for (int n = 0; xecp && n < 32; n++) {
        volatile u32 *c = (volatile u32 *)(x->cap + xecp);
        if ((c[0] & 0xFF) == 1) {
            c[0] |= 1u << 24;
            u32 t = zegar_ms();
            while ((c[0] & (1u << 16)) && zegar_ms() - t < 1000) ;
            c[1] = 0xE0000000u;                          /* wyłącz SMI, wyczyść zgłoszenia */
            break;
        }
        u32 nast = (c[0] >> 8) & 0xFF;
        if (!nast) break;
        xecp += nast << 2;
    }

    x->op[0] &= ~1u;                                     /* stop */
    u32 t = zegar_ms();
    while (!(x->op[1] & 1) && zegar_ms() - t < 100) ;
    x->op[0] |= 2;                                       /* reset */
    czekaj_ms(2);
    t = zegar_ms();
    while (((x->op[0] & 2) || (x->op[1] & (1u << 11))) && zegar_ms() - t < 1000) ;
    if (x->op[1] & (1u << 11)) return 0;

    x->maks_slot = (int)(hcs1 & 0xFF);
    if (x->maks_slot > MAX_SLOT) x->maks_slot = MAX_SLOT;
    x->op[0x38 / 4] = (u32)x->maks_slot;
    x->dcbaa = usb_dma(4096);
    u32 sp = ((hcs2 >> 27) & 0x1F) | (((hcs2 >> 21) & 0x1F) << 5);
    if (sp) {
        u32 *tab = usb_dma(sp * 8);
        for (u32 i = 0; i < sp && tab; i++) { tab[2 * i] = (u32)usb_dma(4096); tab[2 * i + 1] = 0; }
        x->dcbaa[0] = (u32)tab;
    }
    x->op[0x30 / 4] = (u32)x->dcbaa; x->op[0x34 / 4] = 0;
    if (!ring_init(&x->kom)) return 0;
    x->op[0x18 / 4] = (u32)x->kom.trb | 1; x->op[0x1C / 4] = 0;

    x->ev = usb_dma(EV_N * 16);
    u32 *erst = usb_dma(64);
    if (!x->ev || !erst || !x->dcbaa) return 0;
    erst[0] = (u32)x->ev; erst[1] = 0; erst[2] = EV_N;
    x->ev_idx = 0; x->ev_cykl = 1;
    x->ir[2] = 1;                                        /* ERSTSZ */
    x->ir[6] = (u32)x->ev; x->ir[7] = 0;                 /* ERDP */
    x->ir[4] = (u32)erst; x->ir[5] = 0;                  /* ERSTBA */
    x->op[0] |= 1;                                       /* start */
    t = zegar_ms();
    while ((x->op[1] & 1) && zegar_ms() - t < 100) ;

    usb_hc_t *hc = usb_nowy_kontroler();
    if (!hc) return 0;
    nxk++;
    x->hc = hc;
    hc->nazwa = "xHCI (USB 3)";
    hc->vid = (u16)id; hc->did = (u16)(id >> 16);
    hc->nportow = (int)(hcs1 >> 24);
    hc->dane = x;
    hc->kontrolny = x_kontrolny;
    hc->konfiguruj = x_konfiguruj;
    hc->start = x_start;
    hc->odpytaj = x_odpytaj;
    hc->adresuj = x_adresuj;
    hc->hub = x_hub;
    hc->usun = x_usun;

    for (int p = 1; p <= hc->nportow; p++) {             /* zasilanie portów */
        volatile u32 *ps = portsc(x, p);
        if (!(*ps & (1u << 9))) *ps = PORT_NEUTRALNIE(*ps) | (1u << 9);
    }
    czekaj_ms(150);
    zdarzenia(x);
    x->port_zmiana = 0;
    for (int p = 1; p <= hc->nportow; p++) if (*portsc(x, p) & 1) port_urzadzenie(x, p);
    return 1;
}
