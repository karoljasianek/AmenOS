/* AmenOS - stos sieciowy: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP
 * Prosty i jednowątkowy: jedno połączenie TCP naraz, odpytywanie karty. */
#include "kernel.h"

u32 moje_ip, maska, brama, dns_ip;      /* w kolejności sieciowej (jak w pakietach) */
u32 dns_wlasny;                         /* DNS ustawiony ręcznie - ma pierwszeństwo przed DHCP */
int siec_gotowa;

#define PAK __attribute__((packed))
typedef struct { u8 cel[6], zrodlo[6]; u16 typ; } PAK eth_t;
typedef struct { u16 htyp, ptyp; u8 hdl, pdl; u16 op; u8 sha[6]; u32 spa; u8 tha[6]; u32 tpa; } PAK arp_t;
typedef struct { u8 wersja, tos; u16 dl, id, frag; u8 ttl, proto; u16 suma; u32 zrodlo, cel; } PAK ip_t;
typedef struct { u16 zport, cport, dl, suma; } PAK udp_t;
typedef struct { u16 zport, cport; u32 seq, ack; u8 off, flagi; u16 okno, suma, pilne; } PAK tcp_t;
typedef struct { u8 typ, kod; u16 suma, id, nr; } PAK icmp_t;

static u16 hs(u16 x) { return (u16)((x >> 8) | (x << 8)); }
static u32 hl(u32 x) { return (x >> 24) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24); }
#define IP(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

static const u8 MAC_ROZGL[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static u8 ramka_we[2048], ramka_wy[1600];

/* ---------- pomocnicze ---------- */

u32 losowa(void) {
    static u32 s;
    s ^= zegar_ms() * 2654435761u;
    outb(0x43, 0x00); s ^= inb(0x40); s ^= (u32)inb(0x40) << 8;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

void ip_tekst(u32 ip, char *b) {
    ksprintf(b, 16, "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24);
}

int ip_z_tekstu(const char *s, u32 *ip) {
    u32 w = 0; int cz = 0;
    for (int i = 0; i < 4; i++) {
        int v = 0, cyfr = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); cyfr++; }
        if (!cyfr || v > 255) return 0;
        w |= (u32)v << (8 * i); cz++;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    if (*s) return 0;
    *ip = w;
    return cz == 4;
}

static u32 suma_czesciowa(const void *d, u32 dl, u32 s) {
    const u8 *p = d;
    while (dl > 1) { s += (u32)(p[0] << 8 | p[1]); p += 2; dl -= 2; }
    if (dl) s += (u32)(p[0] << 8);
    return s;
}
static u16 suma_koncowa(u32 s) {
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return hs((u16)~s);
}

/* ---------- ARP ---------- */

static struct { u32 ip; u8 mac[6]; } arp_tab[16];
static int arp_n;

static void arp_zapamietaj(u32 ip, const u8 *mac) {
    for (int i = 0; i < arp_n; i++) if (arp_tab[i].ip == ip) { memcpy(arp_tab[i].mac, mac, 6); return; }
    int i = arp_n < 16 ? arp_n++ : (int)(losowa() % 16);
    arp_tab[i].ip = ip; memcpy(arp_tab[i].mac, mac, 6);
}

static void eth_wyslij(const u8 *cel, u16 typ, const void *dane, u32 dl) {
    eth_t *e = (eth_t *)ramka_wy;
    memcpy(e->cel, cel, 6); memcpy(e->zrodlo, karta_mac, 6); e->typ = hs(typ);
    memcpy(ramka_wy + sizeof(eth_t), dane, dl);
    e1000_wyslij(ramka_wy, sizeof(eth_t) + dl);
}

static void arp_wyslij(u16 op, const u8 *tha, u32 tpa, const u8 *cel) {
    arp_t a;
    a.htyp = hs(1); a.ptyp = hs(0x0800); a.hdl = 6; a.pdl = 4; a.op = hs(op);
    memcpy(a.sha, karta_mac, 6); a.spa = moje_ip;
    memcpy(a.tha, tha, 6); a.tpa = tpa;
    eth_wyslij(cel, 0x0806, &a, sizeof a);
}

/* ---------- odbiór i rozdział pakietów ---------- */

static struct { u16 port; int jest; u8 dane[1500]; u32 dl; u32 od_ip; u16 od_portu; } udp_we;
static struct { u16 id, nr; int jest; } ping_we;

typedef struct {
    int stan;                       /* 0 zamknięte, 1 SYN wysłany, 2 połączone */
    u32 ip; u16 lport, rport;
    u32 snd_una, snd_nxt, rcv_nxt;
    u8 *bufor; u32 rozmiar, ile;
    int fin, rst;
    u32 ostatnio;                   /* czas ostatnich danych */
} tcp_pol_t;
static tcp_pol_t tcp;

static void tcp_segment(u8 flagi, u32 seq, const void *dane, u32 dl);
static void ip_wyslij(u32 cel, u8 proto, const void *dane, u32 dl);

static int seq_wiekszy(u32 a, u32 b) { return (int)(a - b) > 0; }

static void tcp_obsluz(const ip_t *ip, const tcp_t *t, u32 dl_seg) {
    if (!tcp.stan || ip->zrodlo != tcp.ip || hs(t->cport) != tcp.lport || hs(t->zport) != tcp.rport) return;
    u32 naglowek = (u32)(t->off >> 4) * 4;
    if (naglowek > dl_seg) return;
    u32 dl = dl_seg - naglowek;
    const u8 *dane = (const u8 *)t + naglowek;
    u32 seq = hl(t->seq), ack = hl(t->ack);
    u8 f = t->flagi;

    if (f & 0x04) { tcp.rst = 1; tcp.stan = 0; return; }                 /* RST */
    if (tcp.stan == 1) {
        if ((f & 0x12) == 0x12 && ack == tcp.snd_nxt) {                   /* SYN+ACK */
            tcp.rcv_nxt = seq + 1; tcp.snd_una = ack; tcp.stan = 2;
            tcp_segment(0x10, tcp.snd_nxt, 0, 0);
        }
        return;
    }
    if ((f & 0x12) == 0x12) { tcp_segment(0x10, tcp.snd_nxt, 0, 0); return; }  /* powtórzony SYN+ACK */
    if ((f & 0x10) && seq_wiekszy(ack, tcp.snd_una) && !seq_wiekszy(ack, tcp.snd_nxt)) tcp.snd_una = ack;

    if (dl || (f & 0x01)) {
        if (seq == tcp.rcv_nxt) {
            u32 miejsce = tcp.rozmiar - tcp.ile, wez = dl < miejsce ? dl : miejsce;
            memcpy(tcp.bufor + tcp.ile, dane, wez);
            tcp.ile += wez; tcp.rcv_nxt += wez;
            if (wez) tcp.ostatnio = zegar_ms();
            if ((f & 0x01) && wez == dl) { tcp.rcv_nxt++; tcp.fin = 1; }
        }
        tcp_segment(0x10, tcp.snd_nxt, 0, 0);                             /* potwierdzenie */
    }
}

static void obsluz_ramke(u8 *r, int dl) {
    if (dl < (int)sizeof(eth_t)) return;
    eth_t *e = (eth_t *)r;
    u16 typ = hs(e->typ);
    u8 *p = r + sizeof(eth_t);
    dl -= sizeof(eth_t);

    if (typ == 0x0806 && dl >= (int)sizeof(arp_t)) {
        arp_t *a = (arp_t *)p;
        if (a->spa) arp_zapamietaj(a->spa, a->sha);
        if (hs(a->op) == 1 && moje_ip && a->tpa == moje_ip) arp_wyslij(2, a->sha, a->spa, a->sha);
        return;
    }
    if (typ != 0x0800 || dl < (int)sizeof(ip_t)) return;
    ip_t *ip = (ip_t *)p;
    u32 ihl = (u32)(ip->wersja & 0x0F) * 4;
    u32 cala = hs(ip->dl);
    if ((ip->wersja >> 4) != 4 || ihl < 20 || cala > (u32)dl || cala < ihl) return;
    if (moje_ip && ip->cel != moje_ip && ip->cel != 0xFFFFFFFFu) return;
    if (hs(ip->frag) & 0x3FFF) return;                                    /* fragmenty pomijamy */
    u8 *ladunek = p + ihl;
    u32 ldl = cala - ihl;

    if (ip->proto == 1 && ldl >= sizeof(icmp_t)) {
        icmp_t *c = (icmp_t *)ladunek;
        if (c->typ == 8 && moje_ip) {                                     /* ktoś nas pinguje */
            u8 odp[1500];
            if (ldl > sizeof odp) return;
            memcpy(odp, ladunek, ldl);
            ((icmp_t *)odp)->typ = 0; ((icmp_t *)odp)->suma = 0;
            ((icmp_t *)odp)->suma = suma_koncowa(suma_czesciowa(odp, ldl, 0));
            ip_wyslij(ip->zrodlo, 1, odp, ldl);
        } else if (c->typ == 0 && c->id == ping_we.id && c->nr == ping_we.nr) ping_we.jest = 1;
    } else if (ip->proto == 17 && ldl >= sizeof(udp_t)) {
        udp_t *u = (udp_t *)ladunek;
        u32 udl = hs(u->dl);
        if (udl < sizeof(udp_t) || udl > ldl) return;
        if (udp_we.port && hs(u->cport) == udp_we.port && !udp_we.jest) {
            udp_we.dl = udl - sizeof(udp_t);
            if (udp_we.dl > sizeof udp_we.dane) udp_we.dl = sizeof udp_we.dane;
            memcpy(udp_we.dane, ladunek + sizeof(udp_t), udp_we.dl);
            udp_we.od_ip = ip->zrodlo; udp_we.od_portu = hs(u->zport);
            udp_we.jest = 1;
        }
    } else if (ip->proto == 6 && ldl >= sizeof(tcp_t)) {
        tcp_obsluz(ip, (tcp_t *)ladunek, ldl);
    }
}

void siec_odbieraj(void) {
    int dl;
    while ((dl = e1000_odbierz(ramka_we)) != 0)
        if (dl > 0) obsluz_ramke(ramka_we, dl);
}

static int arp_znajdz(u32 ip, u8 *mac) {
    for (int i = 0; i < arp_n; i++) if (arp_tab[i].ip == ip) { memcpy(mac, arp_tab[i].mac, 6); return 1; }
    return 0;
}

static int arp_rozwiaz(u32 ip, u8 *mac) {
    if (arp_znajdz(ip, mac)) return 1;
    static const u8 zero[6];
    for (int proba = 0; proba < 3; proba++) {
        arp_wyslij(1, zero, ip, MAC_ROZGL);
        u32 t0 = zegar_ms();
        while (zegar_ms() - t0 < 1000) { siec_odbieraj(); if (arp_znajdz(ip, mac)) return 1; }
    }
    return 0;
}

/* ---------- IP, UDP ---------- */

static void ip_wyslij(u32 cel, u8 proto, const void *dane, u32 dl) {
    static u8 pak[1500];
    static u16 nr_id;
    u8 mac[6];
    if (dl + sizeof(ip_t) > sizeof pak) return;
    if (cel == 0xFFFFFFFFu) memcpy(mac, MAC_ROZGL, 6);
    else {
        u32 nast = ((cel & maska) == (moje_ip & maska)) ? cel : brama;
        if (!arp_rozwiaz(nast, mac)) return;
    }
    ip_t *h = (ip_t *)pak;
    h->wersja = 0x45; h->tos = 0; h->dl = hs((u16)(sizeof(ip_t) + dl)); h->id = hs(++nr_id);
    h->frag = hs(0x4000); h->ttl = 64; h->proto = proto; h->suma = 0;
    h->zrodlo = moje_ip; h->cel = cel;
    h->suma = suma_koncowa(suma_czesciowa(h, sizeof(ip_t), 0));
    memcpy(pak + sizeof(ip_t), dane, dl);
    eth_wyslij(mac, 0x0800, pak, sizeof(ip_t) + dl);
}

static void udp_wyslij(u32 cel, u16 zport, u16 cport, const void *dane, u32 dl) {
    static u8 pak[1480];
    if (dl + sizeof(udp_t) > sizeof pak) return;
    udp_t *u = (udp_t *)pak;
    u->zport = hs(zport); u->cport = hs(cport); u->dl = hs((u16)(sizeof(udp_t) + dl)); u->suma = 0;
    memcpy(pak + sizeof(udp_t), dane, dl);
    ip_wyslij(cel, 17, pak, sizeof(udp_t) + dl);
}

static int udp_czekaj(u32 ms) {
    u32 t0 = zegar_ms();
    while (zegar_ms() - t0 < ms) { siec_odbieraj(); if (udp_we.jest) return 1; }
    return 0;
}

/* ---------- DHCP ---------- */

static u32 dhcp_xid;

static u32 dhcp_pakiet(u8 *b, u8 typ, u32 prosba_ip, u32 serwer) {
    memset(b, 0, 300);
    b[0] = 1; b[1] = 1; b[2] = 6;
    memcpy(b + 4, &dhcp_xid, 4);
    b[10] = 0x80;                                          /* odpowiedź rozgłoszeniowa */
    memcpy(b + 28, karta_mac, 6);
    b[236] = 99; b[237] = 130; b[238] = 83; b[239] = 99;    /* magiczne ciasteczko */
    u32 i = 240;
    b[i++] = 53; b[i++] = 1; b[i++] = typ;
    if (prosba_ip) { b[i++] = 50; b[i++] = 4; memcpy(b + i, &prosba_ip, 4); i += 4; }
    if (serwer) { b[i++] = 54; b[i++] = 4; memcpy(b + i, &serwer, 4); i += 4; }
    b[i++] = 12; b[i++] = 6; memcpy(b + i, "amenos", 6); i += 6;
    b[i++] = 55; b[i++] = 3; b[i++] = 1; b[i++] = 3; b[i++] = 6;
    b[i++] = 255;
    return i < 300 ? 300 : i;
}

/* zwraca typ wiadomości DHCP i wypełnia znalezione opcje */
static int dhcp_czytaj(const u8 *b, u32 dl, u32 *twoje, u32 *serwer, u32 *m, u32 *r, u32 *d) {
    if (dl < 240 || b[0] != 2 || k_memcmp(b + 4, &dhcp_xid, 4)) return 0;
    if (b[236] != 99 || b[237] != 130 || b[238] != 83 || b[239] != 99) return 0;
    memcpy(twoje, b + 16, 4);
    int typ = 0;
    for (u32 i = 240; i + 1 < dl && b[i] != 255;) {
        if (b[i] == 0) { i++; continue; }
        u8 o = b[i], l = b[i + 1];
        const u8 *v = b + i + 2;
        if (i + 2 + l > dl) break;
        if (o == 53 && l >= 1) typ = v[0];
        else if (o == 54 && l >= 4) memcpy(serwer, v, 4);
        else if (o == 1 && l >= 4) memcpy(m, v, 4);
        else if (o == 3 && l >= 4) memcpy(r, v, 4);
        else if (o == 6 && l >= 4) memcpy(d, v, 4);
        i += 2u + l;
    }
    return typ;
}

static int dhcp(void) {
    static u8 b[400];
    for (int proba = 0; proba < 3; proba++) {
        dhcp_xid = losowa();
        moje_ip = 0;
        u32 twoje = 0, serwer = 0, m = 0, r = 0, d = 0;
        udp_we.port = 68; udp_we.jest = 0;
        udp_wyslij(0xFFFFFFFFu, 68, 67, b, dhcp_pakiet(b, 1, 0, 0));          /* DISCOVER */
        int typ = 0;
        u32 t0 = zegar_ms();
        while (zegar_ms() - t0 < 3000 && typ != 2) {
            if (!udp_czekaj(100)) continue;
            typ = dhcp_czytaj(udp_we.dane, udp_we.dl, &twoje, &serwer, &m, &r, &d);
            udp_we.jest = 0;
        }
        if (typ != 2) continue;
        udp_wyslij(0xFFFFFFFFu, 68, 67, b, dhcp_pakiet(b, 3, twoje, serwer));  /* REQUEST */
        typ = 0;
        t0 = zegar_ms();
        while (zegar_ms() - t0 < 3000 && typ != 5 && typ != 6) {
            if (!udp_czekaj(100)) continue;
            typ = dhcp_czytaj(udp_we.dane, udp_we.dl, &twoje, &serwer, &m, &r, &d);
            udp_we.jest = 0;
        }
        udp_we.port = 0;
        if (typ != 5) continue;
        moje_ip = twoje; maska = m ? m : IP(255, 255, 255, 0); brama = r; dns_ip = dns_wlasny ? dns_wlasny : (d ? d : r);
        return 1;
    }
    udp_we.port = 0;
    return 0;
}

int siec_start(void) {
    if (siec_gotowa) return 0;
    if (!e1000_init()) return -1;
    u32 t0 = zegar_ms();
    while (!e1000_lacze() && zegar_ms() - t0 < 3000) ;
    if (!e1000_lacze()) return -2;
    if (!dhcp()) return -3;
    siec_gotowa = 1;
    return 0;
}

const char *siec_blad(int kod) {
    switch (kod) {
    case -1: return "Nie znaleziono karty sieciowej Intel PRO/1000 (e1000).";
    case -2: return "Brak połączenia - sprawdź kabel sieciowy.";
    case -3: return "Serwer DHCP nie odpowiedział - nie udało się uzyskać adresu IP.";
    }
    return "Nieznany błąd sieci.";
}

/* ---------- DNS ---------- */

static u32 dns_pomin_nazwe(const u8 *b, u32 dl, u32 i) {
    while (i < dl) {
        if (b[i] == 0) return i + 1;
        if ((b[i] & 0xC0) == 0xC0) return i + 2;
        i += (u32)b[i] + 1;
    }
    return dl;
}

int dns_zapytaj(const char *nazwa, u32 *wynik) {
    if (ip_z_tekstu(nazwa, wynik)) return 1;
    static u8 q[300];
    u16 id = (u16)losowa();
    memset(q, 0, 12);
    q[0] = (u8)(id >> 8); q[1] = (u8)id; q[2] = 0x01; q[5] = 1;
    u32 i = 12;
    const char *s = nazwa;
    while (*s) {
        const char *k = s;
        while (*k && *k != '.') k++;
        u32 l = (u32)(k - s);
        if (l == 0 || l > 63 || i + l + 6 > sizeof q) return 0;
        q[i++] = (u8)l; memcpy(q + i, s, l); i += l;
        s = *k ? k + 1 : k;
    }
    q[i++] = 0; q[i++] = 0; q[i++] = 1; q[i++] = 0; q[i++] = 1;
    u16 port = (u16)(49152 + losowa() % 16000);
    for (int proba = 0; proba < 3; proba++) {
        udp_we.port = port; udp_we.jest = 0;
        udp_wyslij(dns_ip, port, 53, q, i);
        u32 t0 = zegar_ms();
        while (zegar_ms() - t0 < 2500) {
            if (!udp_czekaj(100)) continue;
            const u8 *b = udp_we.dane; u32 dl = udp_we.dl;
            udp_we.jest = 0;
            if (dl < 12 || b[0] != (u8)(id >> 8) || b[1] != (u8)id) continue;
            udp_we.port = 0;
            if ((b[3] & 0x0F) != 0) return 0;                  /* nazwa nie istnieje */
            u32 an = (u32)(b[6] << 8 | b[7]), qd = (u32)(b[4] << 8 | b[5]);
            u32 j = 12;
            for (u32 k = 0; k < qd; k++) j = dns_pomin_nazwe(b, dl, j) + 4;
            for (u32 k = 0; k < an && j < dl; k++) {
                j = dns_pomin_nazwe(b, dl, j);
                if (j + 10 > dl) break;
                u16 typ = (u16)(b[j] << 8 | b[j + 1]);
                u16 rdl = (u16)(b[j + 8] << 8 | b[j + 9]);
                j += 10;
                if (typ == 1 && rdl == 4 && j + 4 <= dl) { memcpy(wynik, b + j, 4); return 1; }
                j += rdl;
            }
            return 0;
        }
    }
    udp_we.port = 0;
    return 0;
}

/* ---------- ICMP ping ---------- */

int ping(u32 cel, u32 *ms) {
    static u8 pak[64];
    static u16 nr;
    icmp_t *c = (icmp_t *)pak;
    memset(pak, 0, sizeof pak);
    c->typ = 8; c->id = hs(0xA3E1); c->nr = hs(++nr);
    memcpy(pak + sizeof(icmp_t), "AmenOS - Szczesc Boze!", 22);
    c->suma = suma_koncowa(suma_czesciowa(pak, sizeof pak, 0));
    ping_we.id = c->id; ping_we.nr = c->nr; ping_we.jest = 0;
    u32 t0 = zegar_ms();
    ip_wyslij(cel, 1, pak, sizeof pak);
    while (zegar_ms() - t0 < 2000) {
        siec_odbieraj();
        if (ping_we.jest) { *ms = zegar_ms() - t0; return 1; }
    }
    return 0;
}

/* ---------- TCP (klient, jedno połączenie) ---------- */

static void tcp_segment(u8 flagi, u32 seq, const void *dane, u32 dl) {
    u8 pak[1500];                               /* na stosie: wywołanie może się zagnieździć */
    u32 opcje = (flagi & 0x02) ? 4 : 0;
    tcp_t *t = (tcp_t *)pak;
    t->zport = hs(tcp.lport); t->cport = hs(tcp.rport);
    t->seq = hl(seq); t->ack = hl(tcp.rcv_nxt);
    t->off = (u8)(((sizeof(tcp_t) + opcje) / 4) << 4);
    t->flagi = flagi;
    u32 wolne = tcp.rozmiar - tcp.ile;
    t->okno = hs((u16)(wolne > 65535 ? 65535 : wolne));
    t->suma = 0; t->pilne = 0;
    if (opcje) { u8 *o = pak + sizeof(tcp_t); o[0] = 2; o[1] = 4; o[2] = 0x05; o[3] = 0xB4; }   /* MSS 1460 */
    memcpy(pak + sizeof(tcp_t) + opcje, dane, dl);
    u32 cala = sizeof(tcp_t) + opcje + dl;
    u8 pseudo[12];
    memcpy(pseudo, &moje_ip, 4); memcpy(pseudo + 4, &tcp.ip, 4);
    pseudo[8] = 0; pseudo[9] = 6; pseudo[10] = (u8)(cala >> 8); pseudo[11] = (u8)cala;
    t->suma = suma_koncowa(suma_czesciowa(pak, cala, suma_czesciowa(pseudo, 12, 0)));
    ip_wyslij(tcp.ip, 6, pak, cala);
}

int tcp_polacz(u32 ip, u16 port, u8 *bufor, u32 rozmiar) {
    memset(&tcp, 0, sizeof tcp);
    tcp.ip = ip; tcp.rport = port; tcp.lport = (u16)(49152 + losowa() % 16000);
    tcp.bufor = bufor; tcp.rozmiar = rozmiar;
    u32 iss = losowa();
    tcp.snd_una = iss; tcp.snd_nxt = iss + 1;
    tcp.stan = 1;
    for (int proba = 0; proba < 5; proba++) {
        tcp_segment(0x02, iss, 0, 0);
        u32 t0 = zegar_ms();
        while (zegar_ms() - t0 < 1500) {
            siec_odbieraj();
            if (tcp.stan == 2) { tcp.ostatnio = zegar_ms(); return 1; }
            if (tcp.rst) return 0;
        }
    }
    tcp.stan = 0;
    return 0;
}

int tcp_wyslij(const void *dane, u32 dl) {
    const u8 *p = dane;
    while (dl) {
        u32 kawalek = dl > 1400 ? 1400 : dl;
        u32 seq = tcp.snd_nxt;
        tcp.snd_nxt += kawalek;
        int ok = 0;
        for (int proba = 0; proba < 5 && !ok; proba++) {
            tcp_segment(0x18, seq, p, kawalek);
            u32 t0 = zegar_ms();
            while (zegar_ms() - t0 < 1500) {
                siec_odbieraj();
                if (!seq_wiekszy(tcp.snd_nxt, tcp.snd_una)) { ok = 1; break; }
                if (tcp.rst || !tcp.stan) return 0;
            }
        }
        if (!ok) return 0;
        p += kawalek; dl -= kawalek;
    }
    return 1;
}

u32 tcp_odebrano(void) { return tcp.ile; }
int tcp_zakonczone(void) { return tcp.fin || tcp.rst || !tcp.stan; }
u32 tcp_cisza_ms(void) { return zegar_ms() - tcp.ostatnio; }

void tcp_zamknij(void) {
    if (tcp.stan == 2) {
        tcp_segment(0x11, tcp.snd_nxt, 0, 0);
        tcp.snd_nxt++;
        u32 t0 = zegar_ms();
        while (zegar_ms() - t0 < 300) siec_odbieraj();
    }
    tcp.stan = 0;
}
