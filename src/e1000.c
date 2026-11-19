/* AmenOS - sterownik karty sieciowej Intel PRO/1000 (rodzina 8254x, "e1000")
 * Karta domyślna w QEMU i VirtualBox, spotykana też w starszych komputerach.
 * Praca bez przerwań: system odpytuje kartę (polling). */
#include "kernel.h"

/* rejestry */
#define CTRL   0x0000
#define STATUS 0x0008
#define EERD   0x0014
#define ICR    0x00C0
#define IMC    0x00D8
#define RCTL   0x0100
#define TCTL   0x0400
#define TIPG   0x0410
#define RDBAL  0x2800
#define RDBAH  0x2804
#define RDLEN  0x2808
#define RDH    0x2810
#define RDT    0x2818
#define TDBAL  0x3800
#define TDBAH  0x3804
#define TDLEN  0x3808
#define TDH    0x3810
#define TDT    0x3818
#define MTA    0x5200
#define RAL    0x5400
#define RAH    0x5404

#define RX_N 32
#define TX_N 16

typedef struct { u32 adr_lo, adr_hi; u16 dl, csum; u8 status, bledy; u16 spec; } __attribute__((packed)) rxd_t;
typedef struct { u32 adr_lo, adr_hi; u16 dl; u8 cso, cmd, status, css; u16 spec; } __attribute__((packed)) txd_t;

static volatile rxd_t rx[RX_N] __attribute__((aligned(128)));
static volatile txd_t tx[TX_N] __attribute__((aligned(128)));
static u8 rx_buf[RX_N][2048] __attribute__((aligned(16)));
static u8 tx_buf[TX_N][2048] __attribute__((aligned(16)));
static volatile u32 *mmio;
static int rx_akt, tx_akt;
static u16 model;

u8 karta_mac[6];
int karta_jest;

static u32 rd(u32 r) { return mmio[r / 4]; }
static void wr(u32 r, u32 v) { mmio[r / 4] = v; }

static u32 pci_czytaj(int bus, int dev, int fn, int off) {
    outl(0xCF8, 0x80000000u | (u32)(bus << 16) | (u32)(dev << 11) | (u32)(fn << 8) | (u32)(off & 0xFC));
    return inl(0xCFC);
}
static void pci_pisz(int bus, int dev, int fn, int off, u32 v) {
    outl(0xCF8, 0x80000000u | (u32)(bus << 16) | (u32)(dev << 11) | (u32)(fn << 8) | (u32)(off & 0xFC));
    outl(0xCFC, v);
}

static int obslugiwany(u16 dev) {
    static const u16 lista[] = { 0x100E, 0x100F, 0x1004, 0x1008, 0x1009, 0x100C, 0x100D,
                                 0x1010, 0x1011, 0x1015, 0x1016, 0x1017, 0x101E, 0x1026, 0x1076, 0x1079 };
    for (u32 i = 0; i < sizeof lista / sizeof lista[0]; i++) if (lista[i] == dev) return 1;
    return 0;
}

static u16 eeprom(int adr) {
    wr(EERD, (u32)(adr << 8) | 1);
    u32 t0 = zegar_ms();
    while (zegar_ms() - t0 < 20) {
        u32 v = rd(EERD);
        if (v & 0x10) return (u16)(v >> 16);
    }
    return 0;
}

const char *karta_nazwa(void) {
    switch (model) {
    case 0x100E: return "Intel 82540EM (PRO/1000 MT)";
    case 0x100F: return "Intel 82545EM";
    case 0x1004: return "Intel 82543GC";
    default:     return "Intel PRO/1000 (8254x)";
    }
}

int e1000_init(void) {
    if (karta_jest) return 1;
    int bus = -1, dev = 0, fn = 0;
    for (int b = 0; b < 256 && bus < 0; b++)
        for (int d = 0; d < 32 && bus < 0; d++)
            for (int f = 0; f < 8; f++) {
                u32 id = pci_czytaj(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (f == 0) break; continue; }
                if ((id & 0xFFFF) == 0x8086 && obslugiwany((u16)(id >> 16))) {
                    bus = b; dev = d; fn = f; model = (u16)(id >> 16);
                    break;
                }
            }
    if (bus < 0) return 0;

    u32 bar0 = pci_czytaj(bus, dev, fn, 0x10);
    if (bar0 & 1) return 0;                               /* oczekujemy pamięci MMIO, nie portów */
    mmio = (volatile u32 *)(bar0 & ~0xFu);
    pci_pisz(bus, dev, fn, 0x04, pci_czytaj(bus, dev, fn, 0x04) | 0x06);  /* MMIO + bus master */

    wr(IMC, 0xFFFFFFFF);
    wr(CTRL, rd(CTRL) | (1u << 26));                      /* reset karty */
    czekaj_ms(20);
    wr(IMC, 0xFFFFFFFF);
    rd(ICR);
    wr(CTRL, (rd(CTRL) | (1u << 6) | (1u << 5)) & ~((1u << 3) | (1u << 31) | (1u << 7)));  /* łącze w górę */

    /* adres MAC */
    u32 ral = rd(RAL), rah = rd(RAH);
    if ((rah & 0x80000000u) && (ral || (rah & 0xFFFF))) {
        for (int i = 0; i < 4; i++) karta_mac[i] = (u8)(ral >> (8 * i));
        karta_mac[4] = (u8)rah; karta_mac[5] = (u8)(rah >> 8);
    } else {
        for (int i = 0; i < 3; i++) { u16 w = eeprom(i); karta_mac[2 * i] = (u8)w; karta_mac[2 * i + 1] = (u8)(w >> 8); }
        wr(RAL, karta_mac[0] | (karta_mac[1] << 8) | (karta_mac[2] << 16) | ((u32)karta_mac[3] << 24));
        wr(RAH, karta_mac[4] | (karta_mac[5] << 8) | 0x80000000u);
    }
    for (int i = 0; i < 128; i++) wr(MTA + i * 4, 0);

    /* kolejka odbiorcza */
    for (int i = 0; i < RX_N; i++) { rx[i].adr_lo = (u32)rx_buf[i]; rx[i].adr_hi = 0; rx[i].status = 0; }
    wr(RDBAL, (u32)rx); wr(RDBAH, 0); wr(RDLEN, sizeof rx);
    wr(RDH, 0); wr(RDT, RX_N - 1);
    rx_akt = 0;
    wr(RCTL, (1u << 1) | (1u << 15) | (1u << 26));       /* włącz, rozgłoszenia, bez CRC; bufory 2048 B */

    /* kolejka nadawcza */
    for (int i = 0; i < TX_N; i++) { tx[i].adr_lo = (u32)tx_buf[i]; tx[i].adr_hi = 0; tx[i].status = 1; }
    wr(TDBAL, (u32)tx); wr(TDBAH, 0); wr(TDLEN, sizeof tx);
    wr(TDH, 0); wr(TDT, 0);
    tx_akt = 0;
    wr(TCTL, (1u << 1) | (1u << 3) | (0x0Fu << 4) | (0x40u << 12));
    wr(TIPG, 0x0060200A);

    karta_jest = 1;
    return 1;
}

int e1000_lacze(void) { return karta_jest && (rd(STATUS) & 2); }

void e1000_wyslij(const void *dane, u32 dl) {
    if (!karta_jest || dl > 1514) return;
    int i = tx_akt;
    u32 t0 = zegar_ms();
    while (!(tx[i].status & 1) && zegar_ms() - t0 < 100) ;   /* czekaj na wolny deskryptor */
    memcpy(tx_buf[i], dane, dl);
    tx[i].dl = (u16)dl;
    tx[i].cso = 0; tx[i].css = 0; tx[i].spec = 0;
    tx[i].cmd = 0x0B;                                     /* koniec pakietu, dodaj CRC, raportuj stan */
    tx[i].status = 0;
    tx_akt = (tx_akt + 1) % TX_N;
    wr(TDT, (u32)tx_akt);
}

int e1000_odbierz(u8 *wy) {
    if (!karta_jest) return 0;
    volatile rxd_t *d = &rx[rx_akt];
    if (!(d->status & 1)) return 0;
    int dl = 0;
    if ((d->status & 2) && !d->bledy && d->dl <= 2048) {
        dl = d->dl;
        memcpy(wy, rx_buf[rx_akt], (u32)dl);
    }
    d->status = 0;
    int stary = rx_akt;
    rx_akt = (rx_akt + 1) % RX_N;
    wr(RDT, (u32)stary);
    return dl ? dl : -1;
}
