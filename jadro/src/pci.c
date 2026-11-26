/* AmenOS - dostęp do przestrzeni konfiguracyjnej PCI (mechanizm nr 1, porty 0xCF8/0xCFC) */
#include "usb.h"

u32 pci_czytaj(int bus, int dev, int fn, int off) {
    outl(0xCF8, 0x80000000u | (u32)(bus << 16) | (u32)(dev << 11) | (u32)(fn << 8) | (u32)(off & 0xFC));
    return inl(0xCFC);
}

void pci_pisz(int bus, int dev, int fn, int off, u32 v) {
    outl(0xCF8, 0x80000000u | (u32)(bus << 16) | (u32)(dev << 11) | (u32)(fn << 8) | (u32)(off & 0xFC));
    outl(0xCFC, v);
}
