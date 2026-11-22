/* AmenOS - wspólne definicje USB (kontrolery UHCI i xHCI, urządzenia HID) */
#ifndef AMENOS_USB_H
#define AMENOS_USB_H
#include "kernel.h"

enum { USB_LOW = 0, USB_FULL = 1, USB_HIGH = 2, USB_SUPER = 3 };
enum { HID_KLAWIATURA = 1, HID_MYSZ = 2 };

typedef struct usb_hc usb_hc_t;

typedef struct {
    int typ;                    /* HID_KLAWIATURA / HID_MYSZ */
    int iface, ep, mps, interval;
    u8 *bufor;                  /* bufor raportu (pamięć DMA) */
    int toggle;                 /* UHCI: bit przełączania danych */
    void *td, *qh;              /* UHCI */
    int dci;                    /* xHCI: indeks kontekstu punktu końcowego */
    int ring;                   /* xHCI: numer pierścienia */
} usb_hid_t;

typedef struct {
    int jest;
    usb_hc_t *hc;
    int port, predkosc, adres, mps0;
    int slot;                   /* xHCI */
    u16 vid, pid;
    u8 konfiguracja;
    int nhid;
    usb_hid_t hid[2];
    u8 stare[8];                /* poprzedni raport klawiatury */
    /* koncentratory */
    void *rodzic;               /* usb_dev_t koncentratora albo 0 (port główny) */
    int port_rodzica, poziom;
    u32 trasa;                  /* xHCI: route string */
    int hub, hub_porty;
} usb_dev_t;

struct usb_hc {
    const char *nazwa;
    u16 vid, did;
    int nportow;
    void *dane;
    /* operacje kontrolera */
    int  (*kontrolny)(usb_dev_t *d, const u8 *setup, void *dane, int dl);
    int  (*konfiguruj)(usb_dev_t *d);         /* przygotuj punkty końcowe HID */
    void (*start)(usb_dev_t *d);              /* zacznij odpytywać punkty końcowe */
    void (*odpytaj)(usb_hc_t *hc);
    int  (*adresuj)(usb_dev_t *d);            /* nadaj adres nowemu urządzeniu (także za koncentratorem) */
    void (*hub)(usb_dev_t *d);                /* poinformuj kontroler, że urządzenie jest koncentratorem */
    void (*usun)(usb_dev_t *d);
};

/* usb.c */
void *usb_dma(u32 rozmiar);                    /* pamięć dla kontrolera (wyrównana do 4 KB, wyzerowana) */
usb_dev_t *usb_nowe(usb_hc_t *hc, int port, int predkosc);
void usb_usun(usb_dev_t *d);
usb_dev_t *usb_lista(int *ile);
void usb_odlacz_port(usb_hc_t *hc, int port);
int  usb_zadanie(usb_dev_t *d, u8 typ, u8 req, u16 wartosc, u16 indeks, void *dane, u16 dl);
int  usb_skonfiguruj(usb_dev_t *d);            /* deskryptory, konfiguracja, start HID lub koncentratora */
void usb_podlaczono(usb_hc_t *hc, usb_dev_t *rodzic, int port_glowny, int port_rodzica, int predkosc);
void usb_raport(usb_dev_t *d, int i, const u8 *r, int dl);
usb_hc_t *usb_nowy_kontroler(void);
const char *usb_predkosc_nazwa(int p);

/* pci.c */
u32  pci_czytaj(int bus, int dev, int fn, int off);
void pci_pisz(int bus, int dev, int fn, int off, u32 v);

/* sterowniki kontrolerów */
int  xhci_init(int bus, int dev, int fn);
int  uhci_init(int bus, int dev, int fn);

#define BARIERA() __asm__ volatile("" ::: "memory")

#endif
