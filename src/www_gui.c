/* AmenOS - graficzna przeglądarka internetowa (okno na pulpicie).
 *   1. HTML -> lista elementów (słowa ze stylami, bloki, linki, listy, obrazki),
 *   2. obrazki: pobieranie i dekodowanie (PNG, GIF, JPEG, BMP),
 *   3. układ: elementy -> prostokąty w pikselach (zawijanie wierszy, obrazki w tekście),
 *   4. rysowanie w oknie, przewijanie, linki pod myszą.
 * Strony HTTPS są otwierane przez FrogFind (jak w przeglądarce tekstowej). */
#include "kernel.h"
#include "www.h"
#include "obrazy.h"

#define NARZ    36                     /* pasek narzędzi */
#define STAT    22                     /* pasek stanu */
#define SUWAK   14                     /* pasek przewijania */
#define MARG    18                     /* margines strony */
#define MAX_EL  40000
#define MAX_BOX 40000
#define MAX_LINK 2000
#define MAX_IMG 48
#define TEKST_N (1u << 20)
#define STRONA_N (3u << 20)

enum { E_SLOWO, E_LINIA, E_BLOK, E_WCIECIE, E_PUNKT, E_HR, E_OBRAZ, E_POLE, E_PRZYCISK };
enum { S_B = 1, S_H1 = 2, S_H2 = 4, S_H3 = 8, S_LINK = 16, S_PRE = 32, S_SZARY = 64 };
enum { B_TEKST, B_HR, B_OBRAZ, B_PUNKT, B_POLE, B_PRZYCISK, B_ALT };

typedef struct { u8 typ, styl; short link; u32 tekst; u16 dl, n; short arg; } el_t;
typedef struct { short x, y, w, h; u8 typ, styl; short link; u32 tekst; u16 dl; short obraz; } box_t;
typedef struct { u32 src, alt; int w, h, szer; u32 *piks; int stan; } img_t;      /* stan: 0 czeka, 1 jest, 2 błąd */

static el_t *el;  static int nel;
static box_t *bx; static int nbx;
static char *txt; static u32 ntxt;
static u32 link_href[MAX_LINK]; static int nlink;
static img_t img[MAX_IMG]; static int nimg;
static u8 *strona;
static arena_t tmp_ar, img_ar;
static int CW, CH, pamiec_ok;
static int dok_h, przewin, akt = -1, pod_mysza = -1;
static char adres[ADR], tytul[120], tytul_okna[140], status[200], edycja[ADR];
static int edytuj, zastap;
static char do_otwarcia[ADR];
static int do_z_hist;
static int laduje, przerwij;
static struct { char url[ADR]; int przewin; } hist[32];
static int nhist, phist = -1;
static int ciag_suwak = -1;

/* udostępnione przez gui.c */
void gui_www_brudne(int x, int y, int w, int h);
void gui_rysuj_teraz(void);
int  gui_www_aktywne(void);

static int widok_h(void) { return CH - NARZ - STAT; }
static int tresc_w(void) { return CW - SUWAK - 2 * MARG; }
static void brudne_widok(void) { gui_www_brudne(0, NARZ, CW, widok_h()); }
static void brudne_status(void) { gui_www_brudne(0, CH - STAT, CW, STAT); }
static void ustaw_status(const char *s) { k_strncpy(status, s, sizeof status); brudne_status(); }

/* ====================================================================== */
/*  HTML -> elementy                                                       */
/* ====================================================================== */

static int styl, w_linku, pre, glebokosc, grube, spacja, sl_jest;
static u32 sl_start; static u16 sl_dl, sl_n;

static u32 txt_dodaj(const char *s) {
    u32 o = ntxt, n = k_strlen(s);
    if (ntxt + n + 1 >= TEKST_N) return 0;
    memcpy(txt + ntxt, s, n + 1);
    ntxt += n + 1;
    return o;
}

static void el_dodaj(u8 typ, short arg, short link) {
    if (nel >= MAX_EL) return;
    el_t *e = &el[nel++];
    e->typ = typ; e->styl = (u8)styl; e->link = link; e->arg = arg; e->tekst = 0; e->dl = 0; e->n = 0;
}

static void sl_koniec(void) {
    if (!sl_jest) return;
    sl_jest = 0;
    if (ntxt >= TEKST_N || nel >= MAX_EL) return;
    txt[ntxt++] = 0;
    el_t *e = &el[nel++];
    e->typ = E_SLOWO; e->styl = (u8)styl; e->link = (short)w_linku; e->tekst = sl_start; e->dl = sl_dl; e->n = sl_n;
    e->arg = (short)spacja;
    spacja = 0;
}

static void sl_znak(u32 cp) {
    if (!sl_jest) { sl_start = ntxt; sl_dl = 0; sl_n = 0; sl_jest = 1; }
    char b[4];
    int k = k_utf8_put(cp, b);
    if (ntxt + (u32)k + 2 >= TEKST_N) return;
    memcpy(txt + ntxt, b, (u32)k);
    ntxt += (u32)k; sl_dl = (u16)(sl_dl + k); sl_n++;
}

static void znak(u32 cp) {
    if (pre) {
        if (cp == '\r') return;
        if (cp == '\n') { sl_koniec(); el_dodaj(E_LINIA, 0, -1); return; }
        if (cp == '\t') { for (int i = 0; i < 4; i++) sl_znak(' '); return; }
        if (cp >= 0x20 || cp == ' ') sl_znak(cp);
        return;
    }
    if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) { sl_koniec(); spacja = 1; return; }
    if (cp < 0x20) return;
    sl_znak(cp);
    if (sl_n >= 120) sl_koniec();
}

static void napis(const char *s) { while (*s) znak(k_utf8_next(&s)); }
static void blok(int m) { sl_koniec(); spacja = 0; el_dodaj(E_BLOK, (short)m, -1); }

static int jest_w(const char *n, const char *const *l) { for (; *l; l++) if (!k_strcmp(n, *l)) return 1; return 0; }
static const char *const BLOKI[] = { "div", "section", "article", "main", "header", "footer", "nav", "aside", "form",
    "table", "tr", "center", "figure", "figcaption", "address", "fieldset", "details", "summary", "dl", "dt", "dd",
    "caption", "tbody", "thead", "tfoot", "legend", "menu", 0 };

static void znacznik(const char *n, int zamyk, const u8 *s, const u8 *e) {
    char w[ADR];
    if (!k_strcmp(n, "p")) blok(10);
    else if (!k_strcmp(n, "br")) { sl_koniec(); el_dodaj(E_LINIA, 0, -1); }
    else if (jest_w(n, BLOKI)) blok(0);
    else if (n[0] == 'h' && n[1] >= '1' && n[1] <= '6' && !n[2]) {
        blok(n[1] == '1' ? 16 : 12);
        styl &= ~(S_H1 | S_H2 | S_H3);
        if (!zamyk) styl |= n[1] == '1' ? S_H1 : n[1] == '2' ? S_H2 : S_H3;
        if (zamyk) blok(6);
    } else if (!k_strcmp(n, "b") || !k_strcmp(n, "strong") || !k_strcmp(n, "th")) {
        sl_koniec();
        grube += zamyk ? -1 : 1;
        if (grube < 0) grube = 0;
        if (grube) styl |= S_B; else styl &= ~S_B;
        if (!k_strcmp(n, "th") && !zamyk) spacja = 1;
    } else if (!k_strcmp(n, "td") && !zamyk) { sl_koniec(); spacja = 1; }
    else if (!k_strcmp(n, "blockquote")) { blok(8); el_dodaj(E_WCIECIE, zamyk ? -32 : 32, -1); }
    else if (!k_strcmp(n, "ul") || !k_strcmp(n, "ol")) {
        blok(glebokosc == 0 ? 8 : 0);
        el_dodaj(E_WCIECIE, zamyk ? -26 : 26, -1);
        glebokosc += zamyk ? -1 : 1;
        if (glebokosc < 0) glebokosc = 0;
    } else if (!k_strcmp(n, "li") && !zamyk) { sl_koniec(); spacja = 0; el_dodaj(E_PUNKT, 0, -1); }
    else if (!k_strcmp(n, "a")) {
        sl_koniec();
        w_linku = -1; styl &= ~S_LINK;
        if (!zamyk && nlink < MAX_LINK && atrybut(s, e, "href", w, sizeof w)) {
            link_href[nlink] = txt_dodaj(w);
            w_linku = nlink++;
            styl |= S_LINK;
        }
    } else if (!k_strcmp(n, "img")) {
        char alt[100] = "";
        atrybut(s, e, "alt", alt, sizeof alt);
        if (nimg < MAX_IMG && atrybut(s, e, "src", w, sizeof w) && w[0] && !zaczyna(w, "data:")) {
            sl_koniec();
            img_t *im = &img[nimg];
            im->src = txt_dodaj(w); im->alt = txt_dodaj(alt); im->stan = 0; im->piks = 0;
            char sz[12] = "";
            int ok = 0;
            im->szer = atrybut(s, e, "width", sz, sizeof sz) ? k_atoi(sz, &ok) : 0;
            if (!ok) im->szer = 0;
            el_dodaj(E_OBRAZ, (short)nimg, (short)w_linku);
            nimg++;
        } else if (alt[0]) { znak('['); napis(alt); znak(']'); }
    } else if (!k_strcmp(n, "hr")) { blok(6); el_dodaj(E_HR, 0, -1); blok(6); }
    else if (!k_strcmp(n, "pre")) { blok(8); pre = !zamyk; if (pre) styl |= S_PRE; else styl &= ~S_PRE; }
    else if (!k_strcmp(n, "input") && !zamyk) {
        char typ[16] = "";
        atrybut(s, e, "type", typ, sizeof typ);
        if (!k_strcmp(typ, "hidden") || !k_strcmp(typ, "checkbox") || !k_strcmp(typ, "radio")) return;
        sl_koniec();
        if (!k_strcmp(typ, "submit") || !k_strcmp(typ, "button")) {
            if (!atrybut(s, e, "value", w, 40)) k_strncpy(w, "Wyślij", sizeof w);
            el_dodaj(E_PRZYCISK, 0, -1);
            el[nel - 1].tekst = txt_dodaj(w);
            el[nel - 1].n = (u16)k_utf8_len(w);
        } else el_dodaj(E_POLE, 20, -1);
    }
}

static void parsuj_html(const u8 *p, const u8 *k) {
    styl = 0; w_linku = -1; pre = 0; glebokosc = 0; grube = 0; spacja = 0; sl_jest = 0;
    while (p < k && nel < MAX_EL - 4) {
        if (*p == '<') {
            const u8 *s = p + 1;
            if (s + 3 <= k && s[0] == '!' && s[1] == '-' && s[2] == '-') { const u8 *z = szukaj_ci(s, k, "-->"); p = z ? z + 3 : k; continue; }
            int zamyk = 0;
            if (s < k && *s == '/') { zamyk = 1; s++; }
            char n[16]; int i = 0;
            while (s < k && i < 15 && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9'))) n[i++] = mala((char)*s++);
            n[i] = 0;
            if (!i && !(s < k && (*s == '!' || *s == '?'))) { znak('<'); p++; continue; }
            const u8 *e = s; u8 cudz = 0;
            while (e < k) { if (cudz) { if (*e == cudz) cudz = 0; } else if (*e == '"' || *e == '\'') cudz = *e; else if (*e == '>') break; e++; }
            p = e < k ? e + 1 : k;
            if (!i) continue;
            if (!zamyk && (!k_strcmp(n, "script") || !k_strcmp(n, "style") || !k_strcmp(n, "svg") || !k_strcmp(n, "template") ||
                           !k_strcmp(n, "select") || !k_strcmp(n, "textarea"))) {
                char wz[16]; ksprintf(wz, sizeof wz, "</%s", n);
                const u8 *z = szukaj_ci(p, k, wz);
                if (!k_strcmp(n, "select") || !k_strcmp(n, "textarea")) { sl_koniec(); el_dodaj(E_POLE, 14, -1); }
                p = z ? z : k;
                continue;
            }
            if (!zamyk && !k_strcmp(n, "title")) {
                const u8 *z = szukaj_ci(p, k, "</title"), *q = p;
                u32 d = 0; int sp = 0;
                while (q < (z ? z : k) && d + 4 < sizeof tytul) {
                    u32 cp = (*q == '&') ? encja(&q, k) : znak_z(&q, k);
                    if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t') { sp = d > 0; continue; }
                    if (sp) { tytul[d++] = ' '; sp = 0; }
                    d += (u32)k_utf8_put(cp, tytul + d);
                }
                tytul[d] = 0;
                p = z ? z : k;
                continue;
            }
            znacznik(n, zamyk, s, e);
            continue;
        }
        u32 cp = (*p == '&') ? encja(&p, k) : znak_z(&p, k);
        if (cp) znak(cp);
    }
    sl_koniec();
}

/* ====================================================================== */
/*  Układ strony                                                           */
/* ====================================================================== */

static int skala_st(int s) { return (s & S_H1) ? 2 : 1; }

static int lx0, lx, ly, lwys, lpocz, lprzerwa;

static void l_wiersz(void) {
    if (nbx > lpocz || lwys) {
        if (!lwys) lwys = 20;
        for (int i = lpocz; i < nbx; i++) bx[i].y = (short)(ly + lwys - bx[i].h - (bx[i].typ == B_TEKST ? 2 : 0));
        ly += lwys;
        lprzerwa = 0;
    }
    lpocz = nbx; lx = lx0; lwys = 0;
}

static box_t *l_box(int typ, int w, int h, int styl_b, int link) {
    if (nbx >= MAX_BOX) return 0;
    if (lx > lx0 && lx + w > MARG + tresc_w()) l_wiersz();
    box_t *b = &bx[nbx++];
    b->x = (short)lx; b->y = 0; b->w = (short)w; b->h = (short)h; b->typ = (u8)typ; b->styl = (u8)styl_b;
    b->link = (short)link; b->tekst = 0; b->dl = 0; b->obraz = -1;
    lx += w;
    int wys = h + (typ == B_TEKST ? 4 : 2);
    if (wys > lwys) lwys = wys;
    return b;
}

static void uklad(void) {
    nbx = 0; lx0 = MARG; lx = MARG; ly = 12; lwys = 0; lpocz = 0; lprzerwa = 1000;
    int maks = MARG + tresc_w();
    for (int i = 0; i < nel && nbx < MAX_BOX - 2; i++) {
        el_t *e = &el[i];
        switch (e->typ) {
        case E_SLOWO: {
            if (!e->n) break;
            int sk = skala_st(e->styl), zw = 8 * sk, w = e->n * zw, h = 16 * sk;
            if (e->arg && lx > lx0) {
                if (lx + zw + w > maks) l_wiersz();
                else {
                    int prev_link = nbx > lpocz ? bx[nbx - 1].link : -1;
                    if (prev_link >= 0 && prev_link == e->link && bx[nbx - 1].typ == B_TEKST) bx[nbx - 1].w = (short)(bx[nbx - 1].w + zw);
                    lx += zw;
                }
            }
            const char *t = txt + e->tekst;
            int zostalo = e->n;
            u32 off = e->tekst;
            while (zostalo > 0) {                       /* bardzo długie słowa dzielimy */
                int miesci = (maks - lx) / zw;
                if (miesci < zostalo && lx > lx0 && w <= maks - lx0) { l_wiersz(); miesci = (maks - lx) / zw; }
                int ile = zostalo < miesci ? zostalo : (miesci > 0 ? miesci : 1);
                const char *q = t;
                for (int c = 0; c < ile; c++) k_utf8_next(&q);
                box_t *b = l_box(B_TEKST, ile * zw, h, e->styl, e->link);
                if (!b) break;
                b->tekst = off; b->dl = (u16)(q - t);
                off += (u32)(q - t); t = q; zostalo -= ile;
                if (zostalo > 0) l_wiersz();
            }
            break;
        }
        case E_LINIA: if (nbx == lpocz && !lwys) lwys = 20; l_wiersz(); break;
        case E_BLOK:
            if (nbx > lpocz) l_wiersz();
            if (e->arg > lprzerwa) { ly += e->arg - lprzerwa; lprzerwa = e->arg; }
            break;
        case E_WCIECIE:
            lx0 += e->arg;
            if (lx0 < MARG) lx0 = MARG;
            if (lx0 > MARG + 240) lx0 = MARG + 240;
            if (nbx == lpocz) lx = lx0;
            break;
        case E_PUNKT: {
            if (nbx > lpocz) l_wiersz();
            box_t *b = l_box(B_PUNKT, 0, 16, 0, -1);
            if (b) b->x = (short)(lx0 - 14);
            break;
        }
        case E_HR: {
            if (nbx > lpocz) l_wiersz();
            box_t *b = l_box(B_HR, maks - lx0, 2, 0, -1);
            if (b) lwys = 6;
            l_wiersz();
            break;
        }
        case E_OBRAZ: {
            img_t *im = &img[e->arg];
            if (im->stan == 1) {
                if (e->arg >= 0 && lx > lx0 && lx + im->w > maks) l_wiersz();
                box_t *b = l_box(B_OBRAZ, im->w, im->h, 0, e->link);
                if (b) b->obraz = e->arg;
            } else {
                const char *alt = txt + im->alt;
                int n = (int)k_utf8_len(alt);
                if (n > 40) n = 40;
                box_t *b = l_box(B_ALT, (n ? n * 8 : 0) + 26, 20, 0, e->link);
                if (b) {
                    const char *q = alt;
                    for (int c = 0; c < n; c++) k_utf8_next(&q);
                    b->obraz = e->arg; b->tekst = im->alt; b->dl = (u16)(q - alt);
                }
            }
            if (lx < maks) lx += 4;
            break;
        }
        case E_POLE: l_box(B_POLE, e->arg * 8 + 10, 20, 0, -1); lx += 6; break;
        case E_PRZYCISK: { box_t *b = l_box(B_PRZYCISK, e->n * 8 + 20, 22, 0, -1); if (b) { b->tekst = e->tekst; b->dl = (u16)k_strlen(txt + e->tekst); } lx += 6; break; }
        }
    }
    l_wiersz();
    dok_h = ly + 20;
}

/* ====================================================================== */
/*  Ładowanie stron i obrazków                                             */
/* ====================================================================== */

static const char *STRONA_GUI =
    "<title>AmenOS - strona startowa</title>"
    "<h1>Przeglądarka AmenOS</h1>"
    "<p>Wpisz adres strony w pole u góry albo kliknij link. Możesz też wpisać zwykłe słowa - "
    "zostaną wyszukane w wyszukiwarce <b>FrogFind</b>.</p>"
    "<h2>Kościół w sieci</h2><ul>"
    "<li><a href=\"https://www.vaticannews.va/pl.html\">Vatican News po polsku</a>"
    "<li><a href=\"https://opoka.org.pl\">Opoka - portal katolicki</a>"
    "<li><a href=\"https://www.niedziela.pl\">Tygodnik Katolicki Niedziela</a>"
    "<li><a href=\"https://episkopat.pl\">Konferencja Episkopatu Polski</a>"
    "</ul><h2>Strony przyjazne starszym komputerom</h2><ul>"
    "<li><a href=\"http://frogfind.com\">FrogFind - wyszukiwarka</a>"
    "<li><a href=\"http://68k.news\">68k.news - wiadomości ze świata</a>"
    "<li><a href=\"http://info.cern.ch\">Pierwsza strona WWW na świecie (CERN, 1991)</a>"
    "</ul><h2>Wskazówki</h2><ul>"
    "<li>Kliknij link, aby go otworzyć; przyciski u góry: wstecz, dalej, odśwież, strona startowa."
    "<li>Przewijanie: kółko przewijania nie jest obsługiwane - użyj paska po prawej, strzałek albo PgUp/PgDn."
    "<li>Tab przechodzi między linkami, Enter otwiera zaznaczony, Backspace wraca."
    "<li>Obrazki: PNG, GIF, JPEG i BMP. Strony szyfrowane (https://) otwiera serwis FrogFind, "
    "który zamienia je na prostszą postać. JavaScript i formularze nie są obsługiwane."
    "</ul>";

static void nowa_strona(void) {
    nel = 0; nbx = 0; ntxt = 1; nlink = 0; nimg = 0; tytul[0] = 0;
    przewin = 0; akt = -1; pod_mysza = -1;
    img_ar.zajete = 0;
    txt[0] = 0;
}

static void strona_html(const char *html) {
    kodowanie = UTF8;
    parsuj_html((const u8 *)html, (const u8 *)html + k_strlen(html));
}

static void strona_bledu(const char *t, const char *opis) {
    static char h[1200];
    ksprintf(h, sizeof h, "<title>%s</title><h1>%s</h1><p>%s</p><p><a href=\"" START "\">Strona startowa</a></p>", t, t, opis);
    nowa_strona();
    strona_html(h);
}

static int postep_gui(const char *m) {
    if (m) ustaw_status(m);
    if (gui_www_aktywne()) kbd_poll();          /* Esc w oknie przeglądarki przerywa */
    else ps2_obsluz();                           /* mysz działa dalej */
    if (m) gui_rysuj_teraz();
    return przerwij;
}

/* dekoduje dane obrazka i zapisuje go zmniejszonego do szerokości strony */
static int obraz_zapisz(img_t *im, const u8 *d, u32 n) {
    tmp_ar.zajete = 0;
    u32 *p; int w, h;
    if (!obraz_dekoduj(d, n, &tmp_ar, &p, &w, &h)) return 0;
    if (w < 2 || h < 2) return 0;
    int dw = w, dh = h, maks = tresc_w() - 8;
    if (im->szer > 0 && im->szer < dw) dw = im->szer;
    if (dw > maks) dw = maks;
    dh = h * dw / w;
    if (dh > 900) { dw = dw * 900 / dh; dh = 900; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    u32 *wy = 0;
    if (img_ar.zajete + (u32)(dw * dh) * 4 + 16 <= img_ar.rozmiar) { wy = (u32 *)(img_ar.pam + img_ar.zajete); img_ar.zajete += ((u32)(dw * dh) * 4 + 15) & ~15u; }
    if (!wy) { obraz_blad = "za mało pamięci"; return 0; }
    if (dw == w && dh == h) memcpy(wy, p, (u32)(w * h) * 4);
    else obraz_skaluj(p, w, h, wy, dw, dh);
    im->piks = wy; im->w = dw; im->h = dh; im->stan = 1;
    return 1;
}

static void pobierz_obrazki(void) {
    static www_odp_t o;
    char abs_url[ADR], m[120];
    for (int i = 0; i < nimg && !przerwij; i++) {
        img_t *im = &img[i];
        if (im->stan) continue;
        im->stan = 2;
        if (!url_pelny(adres, txt + im->src, abs_url, sizeof abs_url)) continue;
        ksprintf(m, sizeof m, "Obrazek %d z %d...   (Esc - przerwij)", i + 1, nimg);
        ustaw_status(m);
        gui_rysuj_teraz();
        if (!www_pobierz(abs_url, strona, STRONA_N, &o, postep_gui)) continue;
        if (o.kod >= 400) continue;
        if (obraz_zapisz(im, o.cialo, o.dl)) {
            int stary = przewin;
            uklad();
            przewin = stary;
            brudne_widok();
            gui_rysuj_teraz();
        }
    }
}

static void zapisz_historie(const char *url) {
    if (phist >= 0 && !k_strcmp(hist[phist].url, url)) return;
    if (phist >= 0) hist[phist].przewin = przewin;
    nhist = phist + 1;
    if (nhist == 32) { memcpy(hist, hist + 1, sizeof hist[0] * 31); nhist = 31; }
    k_strncpy(hist[nhist].url, url, ADR);
    hist[nhist].przewin = 0;
    phist = nhist++;
}

static void laduj(const char *url_we, int z_historii) {
    static www_odp_t o;
    char url[ADR];
    k_strncpy(url, url_we, sizeof url);
    laduje = 1; przerwij = 0;
    edytuj = 0;
    if (!pamiec_ok) { laduje = 0; return; }
    if (!k_strcmp(url, START)) {
        nowa_strona();
        strona_html(STRONA_GUI);
        k_strncpy(adres, START, sizeof adres);
    } else {
        ustaw_status("Łączenie...");
        gui_rysuj_teraz();
        int ok = www_pobierz(url, strona, STRONA_N, &o, postep_gui);
        k_strncpy(adres, o.url[0] ? o.url : url, sizeof adres);
        nowa_strona();
        if (!ok) strona_bledu(o.blad_tytul, o.blad);
        else if (zaczyna(o.typ, "image/")) {
            img[0].src = txt_dodaj(adres); img[0].alt = txt_dodaj("obrazek"); img[0].szer = 0; img[0].stan = 2; nimg = 1;
            el_dodaj(E_OBRAZ, 0, -1);
            if (!obraz_zapisz(&img[0], o.cialo, o.dl)) { nowa_strona(); strona_bledu("Nie można wyświetlić obrazka", obraz_blad); }
            else ksprintf(tytul, sizeof tytul, "Obrazek %d x %d", img[0].w, img[0].h);
        } else {
            kodowanie = ustal_kodowanie(o.typ, o.cialo, o.dl);
            if (!o.typ[0] || szukaj_ci((const u8 *)o.typ, (const u8 *)o.typ + k_strlen(o.typ), "html"))
                parsuj_html(o.cialo, o.cialo + o.dl);
            else if (zaczyna(o.typ, "text/")) { pre = 1; styl = S_PRE; const u8 *p = o.cialo; while (p < o.cialo + o.dl) znak(znak_z(&p, o.cialo + o.dl)); sl_koniec(); pre = 0; styl = 0; }
            else { char m[160]; ksprintf(m, sizeof m, "Ten rodzaj pliku (%s) nie może zostać wyświetlony.", o.typ); strona_bledu("Plik nie jest stroną", m); }
            if (o.kod >= 400 && !tytul[0]) ksprintf(tytul, sizeof tytul, "Błąd HTTP %d", o.kod);
        }
    }
    if (!z_historii) zapisz_historie(adres);
    uklad();
    ksprintf(tytul_okna, sizeof tytul_okna, "Przeglądarka - %s", tytul[0] ? tytul : adres);
    gui_www_brudne(-4, -30, CW + 8, CH + 34);
    if (nimg) { gui_rysuj_teraz(); pobierz_obrazki(); }
    ustaw_status(przerwij ? "Przerwano." : "Gotowe.");
    gui_www_brudne(-4, -30, CW + 8, CH + 34);
    laduje = 0;
}

/* ====================================================================== */
/*  Rysowanie                                                              */
/* ====================================================================== */

static u32 kolor_st(int s) {
    if (s & S_LINK) return 0x1A4FC4;
    if (s & (S_H1 | S_H2 | S_H3)) return 0x1C3A70;
    if (s & S_SZARY) return 0x777777;
    return 0x202020;
}

static void tekst_utf8(int x, int y, const char *t, int dl, u32 c, int sk, int gruby) {
    const char *k = t + dl;
    while (t < k) {
        u8 g = vga_map(k_utf8_next(&t));
        fb_znak(x, y, g, c, FB_PRZEZR, sk);
        if (gruby) fb_znak(x + 1, y, g, c, FB_PRZEZR, sk);
        x += 8 * sk;
    }
}

static void przycisk_narz(int x, int y, int w, const char *t, int aktywny) {
    fb_prost_zaokr(x, y, w, 24, 4, 0xB8BEC9);
    fb_prost_zaokr(x + 1, y + 1, w - 2, 22, 3, aktywny ? 0xF4F6FA : 0xE2E5EA);
    fb_tekst(x + (w - fb_tekst_szer(t, 1)) / 2, y + 4, t, aktywny ? 0x1C3A70 : 0xA0A4AC, FB_PRZEZR, 1);
}

static void rysuj_widok(int x0, int y0) {
    int vh = widok_h(), szer = CW - SUWAK;
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    fb_klip_zawez(x0, y0, szer, vh);
    fb_prost(x0, y0, szer, vh, 0xFFFFFF);
    if (!pamiec_ok) {
        fb_tekst(x0 + 20, y0 + 20, "Za mało pamięci na przeglądarkę graficzną (potrzeba ok. 48 MB RAM).", 0xC62828, FB_PRZEZR, 1);
        fb_klip(kx, ky, kw, kh);
        return;
    }
    int lo = 0, hi = nbx;                               /* pierwszy widoczny prostokąt (posortowane wg y) */
    while (lo < hi) { int m = (lo + hi) / 2; if (bx[m].y + bx[m].h + 40 < przewin) lo = m + 1; else hi = m; }
    for (int i = lo; i < nbx; i++) {
        box_t *b = &bx[i];
        if (b->y > przewin + vh + 40) break;
        int x = x0 + b->x, y = y0 + b->y - przewin;
        if (y + b->h < y0 || y > y0 + vh) continue;
        int zazn = b->link >= 0 && b->link == akt;
        switch (b->typ) {
        case B_TEKST:
            if (b->styl & S_PRE) fb_prost(x, y - 1, b->w, b->h + 2, 0xF0F0F2);
            if (zazn) fb_prost(x - 1, y - 1, b->w + 2, b->h + 2, 0xC8DAF5);
            tekst_utf8(x, y, txt + b->tekst, b->dl, kolor_st(b->styl), skala_st(b->styl), (b->styl & (S_B | S_H1 | S_H2 | S_H3)) != 0);
            if (b->link >= 0) fb_prost(x, y + b->h * 15 / 16, b->w, 1, (b->link == pod_mysza || zazn) ? 0xC62828 : 0x1A4FC4);
            break;
        case B_HR: fb_prost(x, y, b->w, 1, 0xC8C8CC); fb_prost(x, y + 1, b->w, 1, 0xF0F0F0); break;
        case B_PUNKT: fb_kolo(x + 4, y + 9, 3, 0x1C3A70); break;
        case B_OBRAZ: {
            img_t *im = &img[b->obraz];
            fb_obraz(x, y, im->w, im->h, im->piks);
            if (b->link >= 0 && (zazn || b->link == pod_mysza)) { fb_ramka(x - 2, y - 2, im->w + 4, im->h + 4, 0x1A4FC4); fb_ramka(x - 1, y - 1, im->w + 2, im->h + 2, 0x1A4FC4); }
            break;
        }
        case B_ALT:
            fb_ramka(x, y, b->w, b->h, 0xC0C4CC);
            fb_ikona(x + 3, y + 2, fb_ikona_nr(5), 1);
            tekst_utf8(x + 22, y + 2, txt + b->tekst, b->dl, b->link >= 0 ? 0x1A4FC4 : 0x808080, 1, 0);
            break;
        case B_POLE: fb_prost(x, y, b->w, b->h, 0xFFFFFF); fb_ramka(x, y, b->w, b->h, 0xA0A4AC); break;
        case B_PRZYCISK: przycisk_narz(x, y, b->w, txt + b->tekst, 1); break;
        }
    }
    fb_klip(kx, ky, kw, kh);
}

void wwwg_rysuj(int x, int y) {
    /* pasek narzędzi */
    fb_gradient(x, y, CW, NARZ, 0xF2F3F6, 0xDDE0E6);
    fb_prost(x, y + NARZ - 1, CW, 1, 0xB8BEC9);
    przycisk_narz(x + 6, y + 6, 28, "◄", phist > 0);
    przycisk_narz(x + 38, y + 6, 28, "►", phist >= 0 && phist < nhist - 1);
    przycisk_narz(x + 70, y + 6, 28, laduje ? "X" : "", 1);
    if (!laduje) {                                           /* strzałka w kółku */
        int cx = x + 84, cy = y + 18;
        fb_okrag(cx, cy, 7, 0x1C3A70, 2);
        fb_prost(cx + 1, cy - 8, 7, 6, 0xF4F6FA);
        fb_prost(cx + 1, cy - 9, 2, 5, 0x1C3A70);
        fb_prost(cx + 1, cy - 9, 5, 2, 0x1C3A70);
    }
    przycisk_narz(x + 102, y + 6, 28, "⌂", 1);
    int ax = x + 138, aw = CW - 146;
    fb_prost(ax, y + 6, aw, 24, edytuj ? 0xFFFFFF : 0xFAFAFC);
    fb_ramka(ax, y + 6, aw, 24, edytuj ? 0x3A6EC0 : 0xA8AEB8);
    const char *a = edytuj ? edycja : adres;
    int miesci = (aw - 16) / 8, n = (int)k_utf8_len(a);
    if (edytuj && n > miesci) { for (int i = 0; i < n - miesci; i++) k_utf8_next(&a); n = miesci; }
    int kx, ky, kw, kh;
    fb_klip_pobierz(&kx, &ky, &kw, &kh);
    fb_klip_zawez(ax + 2, y + 6, aw - 4, 24);
    if (edytuj && zastap && n) fb_prost(ax + 7, y + 9, (n < miesci ? n : miesci) * 8 + 2, 18, 0xC8DAF5);
    fb_tekst(ax + 8, y + 10, a, 0x202020, FB_PRZEZR, 1);
    if (edytuj) fb_prost(ax + 8 + (n < miesci ? n : miesci) * 8, y + 10, 2, 16, 0x202020);
    fb_klip(kx, ky, kw, kh);

    rysuj_widok(x, y + NARZ);

    /* pasek przewijania */
    int vh = widok_h(), sx = x + CW - SUWAK, sy = y + NARZ;
    fb_prost(sx, sy, SUWAK, vh, 0xECEDF0);
    fb_prost(sx, sy, 1, vh, 0xD0D3D8);
    if (dok_h > vh) {
        int th = vh * vh / dok_h; if (th < 24) th = 24;
        int ty = sy + (vh - th) * przewin / (dok_h - vh);
        fb_prost_zaokr(sx + 2, ty + 1, SUWAK - 4, th - 2, 4, ciag_suwak >= 0 ? 0x7A808C : 0xA8AEB8);
    }
    /* pasek stanu */
    int st = y + CH - STAT;
    fb_prost(x, st, CW, STAT, 0xEEF0F3);
    fb_prost(x, st, CW, 1, 0xC8CCD4);
    char s[200];
    if (pod_mysza >= 0 && pod_mysza < nlink && !laduje) {
        char pel[ADR];
        if (url_pelny(adres, txt + link_href[pod_mysza], pel, sizeof pel)) k_strncpy(s, pel, sizeof s);
        else k_strncpy(s, "(link nieobsługiwany)", sizeof s);
    } else k_strncpy(s, status, sizeof s);
    fb_klip_zawez(x, st, CW - 60, STAT);
    fb_tekst(x + 8, st + 4, s, 0x404040, FB_PRZEZR, 1);
    fb_klip(kx, ky, kw, kh);
    if (dok_h > vh) {
        int proc = (przewin + vh) * 100 / dok_h; if (proc > 100) proc = 100;
        char p[8]; ksprintf(p, sizeof p, "%d%%", proc);
        fb_tekst(x + CW - 8 - fb_tekst_szer(p, 1), st + 4, p, 0x808080, FB_PRZEZR, 1);
    }
}

/* ====================================================================== */
/*  Obsługa myszy i klawiatury                                             */
/* ====================================================================== */

static void przewin_do(int p) {
    int maks = dok_h - widok_h();
    if (p > maks) p = maks;
    if (p < 0) p = 0;
    if (p != przewin) { przewin = p; brudne_widok(); gui_www_brudne(CW - SUWAK, NARZ, SUWAK, widok_h()); brudne_status(); }
}

static int link_w(int lx, int ly) {
    if (ly < NARZ || ly >= CH - STAT || lx >= CW - SUWAK) return -1;
    int dy = ly - NARZ + przewin;
    for (int i = 0; i < nbx; i++) {
        box_t *b = &bx[i];
        if (b->y > dy) break;
        if (b->link >= 0 && lx >= b->x && lx < b->x + b->w && dy >= b->y - 2 && dy < b->y + b->h + 2) return b->link;
    }
    return -1;
}

void wwwg_otworz(const char *url) { char u[ADR]; wpisany_adres(url, u, sizeof u); k_strncpy(do_otwarcia, u, sizeof do_otwarcia); }

static void otworz_link(int l) {
    char u[ADR];
    if (l >= 0 && l < nlink && url_pelny(adres, txt + link_href[l], u, sizeof u)) k_strncpy(do_otwarcia, u, sizeof do_otwarcia);
}

static void wstecz(void) { if (phist > 0) { hist[phist].przewin = przewin; phist--; k_strncpy(do_otwarcia, hist[phist].url, ADR); do_z_hist = 1; } }
static void dalej(void) { if (phist >= 0 && phist < nhist - 1) { hist[phist].przewin = przewin; phist++; k_strncpy(do_otwarcia, hist[phist].url, ADR); do_z_hist = 1; } }

void wwwg_klik(int lx, int ly) {
    if (ly < NARZ) {
        if (lx >= 6 && lx < 34) wstecz();
        else if (lx >= 38 && lx < 66) dalej();
        else if (lx >= 70 && lx < 98) { if (laduje) przerwij = 1; else k_strncpy(do_otwarcia, adres, ADR); }
        else if (lx >= 102 && lx < 130) k_strncpy(do_otwarcia, START, ADR);
        else if (lx >= 138) { edytuj = 1; zastap = 1; k_strncpy(edycja, adres, sizeof edycja); gui_www_brudne(0, 0, CW, NARZ); }
        return;
    }
    if (edytuj) { edytuj = 0; gui_www_brudne(0, 0, CW, NARZ); }
    int vh = widok_h();
    if (lx >= CW - SUWAK && ly < CH - STAT) {           /* pasek przewijania */
        if (dok_h <= vh) return;
        int th = vh * vh / dok_h; if (th < 24) th = 24;
        int ty = NARZ + (vh - th) * przewin / (dok_h - vh);
        if (ly < ty) przewin_do(przewin - vh + 40);
        else if (ly > ty + th) przewin_do(przewin + vh - 40);
        else ciag_suwak = ly - ty;
        return;
    }
    int l = link_w(lx, ly);
    if (l >= 0) otworz_link(l);
}

void wwwg_ciagnij(int lx, int ly) {
    (void)lx;
    if (ciag_suwak < 0) return;
    int vh = widok_h();
    if (dok_h <= vh) return;
    int th = vh * vh / dok_h; if (th < 24) th = 24;
    int ty = ly - ciag_suwak - NARZ;
    przewin_do(ty * (dok_h - vh) / (vh - th > 0 ? vh - th : 1));
}

void wwwg_puszczenie(void) { if (ciag_suwak >= 0) { ciag_suwak = -1; gui_www_brudne(CW - SUWAK, NARZ, SUWAK, widok_h()); } }

int wwwg_najedz(int lx, int ly) {
    int l = link_w(lx, ly);
    if (l == pod_mysza) return 0;
    pod_mysza = l;
    brudne_widok(); brudne_status();
    return 1;
}

static void pokaz_link(int l) {
    for (int i = 0; i < nbx; i++)
        if (bx[i].link == l) {
            if (bx[i].y < przewin + 10) przewin_do(bx[i].y - 40);
            else if (bx[i].y + bx[i].h > przewin + widok_h() - 10) przewin_do(bx[i].y - widok_h() / 2);
            break;
        }
    brudne_widok();
}

void wwwg_klawisz(int k) {
    if (laduje) { if (k == KEY_ESC) przerwij = 1; return; }
    if (edytuj) {
        if (k == KEY_ENTER) { edytuj = 0; if (edycja[0]) wwwg_otworz(edycja); }
        else if (k == KEY_ESC) edytuj = 0;
        else if (k == KEY_BKSP) {
            if (zastap) edycja[0] = 0;
            else { int n = (int)k_strlen(edycja); do { n--; } while (n > 0 && ((u8)edycja[n] & 0xC0) == 0x80); if (n < 0) n = 0; edycja[n] = 0; }
            zastap = 0;
        } else if (k >= 0x20 && (k < 0x1000 || k > 0x10FF)) {
            if (zastap) { edycja[0] = 0; zastap = 0; }
            char b[4]; int n = k_utf8_put((u32)k, b), d = (int)k_strlen(edycja);
            if (d + n < (int)sizeof edycja - 1) { memcpy(edycja + d, b, (u32)n); edycja[d + n] = 0; }
        }
        gui_www_brudne(0, 0, CW, NARZ);
        return;
    }
    int vh = widok_h();
    if (k == KEY_DOWN) przewin_do(przewin + 40);
    else if (k == KEY_UP) przewin_do(przewin - 40);
    else if (k == KEY_PGDN || k == ' ') przewin_do(przewin + vh - 40);
    else if (k == KEY_PGUP) przewin_do(przewin - vh + 40);
    else if (k == KEY_HOME) przewin_do(0);
    else if (k == KEY_END) przewin_do(dok_h);
    else if (k == '\t') { if (nlink) { akt = (akt + 1) % nlink; pokaz_link(akt); } }
    else if (k == KEY_ENTER) { if (akt >= 0) otworz_link(akt); }
    else if (k == KEY_BKSP || k == KEY_LEFT) wstecz();
    else if (k == KEY_RIGHT) dalej();
    else if (k >= 0x21 && (k < 0x1000 || k > 0x10FF)) {          /* pisanie od razu trafia do pola adresu */
        edytuj = 1; zastap = 0;
        char b[4]; int n = k_utf8_put((u32)k, b); memcpy(edycja, b, (u32)n); edycja[n] = 0;
        gui_www_brudne(0, 0, CW, NARZ);
    }
}

/* wołane z pętli pulpitu: wykonuje zaległe przejście na stronę */
void wwwg_tik(void) {
    if (!do_otwarcia[0] || laduje) return;
    char u[ADR];
    int z_hist = do_z_hist;
    k_strncpy(u, do_otwarcia, sizeof u);
    do_otwarcia[0] = 0; do_z_hist = 0;
    int p = z_hist && phist >= 0 ? hist[phist].przewin : 0;
    laduj(u, z_hist);
    if (z_hist) przewin_do(p);
}

const char *wwwg_tytul(void) { return tytul_okna; }

int wwwg_init(int cw, int ch) {
    CW = cw; CH = ch;
    k_strncpy(tytul_okna, "Przeglądarka", sizeof tytul_okna);
    k_strncpy(status, "Gotowe.", sizeof status);
    el = pamiec_duza(MAX_EL * sizeof(el_t));
    bx = pamiec_duza(MAX_BOX * sizeof(box_t));
    txt = pamiec_duza(TEKST_N);
    strona = pamiec_duza(STRONA_N);
    img_ar.pam = pamiec_duza(8u << 20); img_ar.rozmiar = 8u << 20;
    tmp_ar.pam = pamiec_duza(24u << 20); tmp_ar.rozmiar = 24u << 20;
    if (!tmp_ar.pam) { tmp_ar.pam = pamiec_duza(6u << 20); tmp_ar.rozmiar = 6u << 20; }
    pamiec_ok = el && bx && txt && strona && img_ar.pam && tmp_ar.pam;
    return pamiec_ok;
}
