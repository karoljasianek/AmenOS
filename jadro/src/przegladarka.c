/* AmenOS - tekstowa przeglądarka internetowa
 * HTTP/1.0, HTML z linkami, przekierowania, historia; strony HTTPS przez serwis FrogFind. */
#include "kernel.h"
#include "znaki_dane.h"
#include "www.h"

#define SZER     78                  /* szerokość tekstu strony */
#define WIDOK    22                  /* wiersze treści na ekranie (2..23) */
#define MAXW     6000
#define MAXL     700

/* ---------- dane strony ---------- */

static u16 strona[MAXW][80];
static int wierszy;
typedef struct { int w0, k0, w1, k1; char href[ADR]; } link_t;
static link_t linki[MAXL];
static int nlink;
static char tytul[100];
static char adres[ADR];
static u8 dane[1u << 20];
static u32 dane_dl;
static int przewin, akt;

static struct { char url[ADR]; int przewin, akt; } hist[24];
static int hist_n;

/* ---------- napisy ---------- */

char mala(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

int zaczyna(const char *s, const char *p) {
    while (*p) if (mala(*s++) != mala(*p++)) return 0;
    return 1;
}

const u8 *szukaj_ci(const u8 *s, const u8 *k, const char *wzor) {
    u32 n = k_strlen(wzor);
    for (; s + n <= k; s++) {
        u32 i = 0;
        while (i < n && mala((char)s[i]) == mala(wzor[i])) i++;
        if (i == n) return s;
    }
    return 0;
}

void dopisz(char *d, u32 max, const char *s) {
    u32 n = k_strlen(d);
    while (*s && n + 1 < max) d[n++] = *s++;
    d[n] = 0;
}

void url_koduj(const char *s, char *o, u32 max) {
    u32 n = 0;
    for (; *s && n + 4 < max; s++) {
        u8 c = (u8)*s;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            o[n++] = (char)c;
        else if (c == ' ') o[n++] = '+';
        else { o[n++] = '%'; o[n++] = "0123456789ABCDEF"[c >> 4]; o[n++] = "0123456789ABCDEF"[c & 15]; }
    }
    o[n] = 0;
}

/* strony HTTPS otwieramy przez FrogFind, który zamienia je na prosty HTML */
void przez_frogfind(const char *https, char *o, u32 max) {
    char kod[ADR];
    url_koduj(https, kod, sizeof kod);
    ksprintf(o, max, FROGFIND "/read.php?a=%s", kod);
}

/* ---------- adresy URL ---------- */

typedef struct { char host[128]; u16 port; char sciezka[ADR]; } url_t;

static int url_rozbierz(const char *u, url_t *o) {
    if (zaczyna(u, "http://")) u += 7;
    else return 0;
    u32 i = 0;
    while (*u && *u != '/' && *u != ':' && *u != '?' && *u != '#' && i + 1 < sizeof o->host) o->host[i++] = mala(*u++);
    o->host[i] = 0;
    if (!i) return 0;
    o->port = 80;
    if (*u == ':') {
        u++; int p = 0;
        while (*u >= '0' && *u <= '9') p = p * 10 + (*u++ - '0');
        if (p <= 0 || p > 65535) return 0;
        o->port = (u16)p;
    }
    i = 0;
    if (*u != '/') o->sciezka[i++] = '/';
    while (*u && *u != '#' && i + 1 < sizeof o->sciezka) {
        char c = *u++;
        if (c == ' ') { if (i + 4 < sizeof o->sciezka) { o->sciezka[i++] = '%'; o->sciezka[i++] = '2'; o->sciezka[i++] = '0'; } }
        else o->sciezka[i++] = c;
    }
    o->sciezka[i] = 0;
    return 1;
}

/* zamienia link (względny lub bezwzględny) na pełny adres http:// */
int url_pelny(const char *baza, const char *href, char *o, u32 max) {
    while (*href == ' ' || *href == '\n' || *href == '\t') href++;
    if (!*href || *href == '#' || zaczyna(href, "javascript:") || zaczyna(href, "mailto:") || zaczyna(href, "tel:"))
        return 0;
    if (zaczyna(href, "about:")) k_strncpy(o, href, max);
    else if (zaczyna(href, "http://")) k_strncpy(o, href, max);
    else if (zaczyna(href, "https://")) przez_frogfind(href, o, max);
    else if (href[0] == '/' && href[1] == '/') { k_strncpy(o, "http:", max); dopisz(o, max, href); }
    else if (zaczyna(baza, "http://")) {
        const char *h = baza + 7;
        while (*h && *h != '/' && *h != '?' && *h != '#') h++;
        u32 dl_hosta = (u32)(h - baza);
        if (href[0] == '/') {
            k_strncpy(o, baza, dl_hosta + 1 < max ? dl_hosta + 1 : max);
            dopisz(o, max, href);
        } else if (href[0] == '?') {
            const char *q = h; while (*q && *q != '?' && *q != '#') q++;
            u32 n = (u32)(q - baza);
            k_strncpy(o, baza, n + 1 < max ? n + 1 : max);
            dopisz(o, max, href);
        } else {
            const char *kon = h, *q = h;
            while (*q && *q != '?' && *q != '#') { if (*q == '/') kon = q + 1; q++; }
            if (kon == h) { k_strncpy(o, baza, dl_hosta + 1 < max ? dl_hosta + 1 : max); dopisz(o, max, "/"); }
            else { u32 n = (u32)(kon - baza); k_strncpy(o, baza, n + 1 < max ? n + 1 : max); }
            while (zaczyna(href, "./")) href += 2;
            dopisz(o, max, href);
        }
    } else return 0;
    char *f = o; while (*f && *f != '#') f++;
    *f = 0;
    return 1;
}

/* ---------- ekran ---------- */

static void pasek(const char *s) {
    vga_fill(0, 24, VGA_W, 1, ' ', ATTR(CZARNY, SZARY));
    vga_puts_at(1, 24, s, ATTR(CZARNY, SZARY));
}

static void rysuj(void) {
    vga_cursor(0);
    vga_fill(0, 0, VGA_W, 1, ' ', ATTR(BIALY, NIEBIESKI));
    char t[100];
    ksprintf(t, sizeof t, "%s", tytul[0] ? tytul : adres);
    t[0] = t[0];
    vga_puts_at(1, 0, t, ATTR(BIALY, NIEBIESKI));
    vga_fill(0, 1, VGA_W, 1, ' ', ATTR(CZARNY, SZARY));
    vga_puts_at(0, 1, " Adres:", ATTR(NIEBIESKI, SZARY));
    vga_puts_at(8, 1, adres, ATTR(CZARNY, SZARY));
    for (int r = 0; r < WIDOK; r++) {
        int w = przewin + r;
        vga_glyph_at(0, 2 + r, ' ', ATTR(SZARY, CZARNY));
        vga_glyph_at(79, 2 + r, ' ', ATTR(SZARY, CZARNY));
        for (int k = 0; k < SZER; k++) {
            u16 c = (w < wierszy) ? strona[w][k] : 0;
            if (!c) c = (u16)(ATTR(SZARY, CZARNY) << 8 | ' ');
            vga_glyph_at(k + 1, 2 + r, (u8)c, (u8)(c >> 8));
        }
    }
    if (akt >= 0 && akt < nlink && linki[akt].w0 >= 0) {
        link_t *L = &linki[akt];
        for (int w = L->w0; w <= L->w1; w++) {
            if (w < przewin || w >= przewin + WIDOK) continue;
            int k0 = (w == L->w0) ? L->k0 : 0, k1 = (w == L->w1) ? L->k1 : SZER - 1;
            for (int k = k0; k <= k1; k++) {
                u16 c = strona[w][k];
                if (!c) continue;
                vga_glyph_at(k + 1, 2 + w - przewin, (u8)c, ATTR(CZARNY, JASNOTURKUSOWY));
            }
        }
    }
    int proc = wierszy <= WIDOK ? 100 : (przewin + WIDOK) * 100 / wierszy;
    if (proc > 100) proc = 100;
    char s[100];
    if (akt >= 0 && akt < nlink) {
        char pel[ADR];
        if (url_pelny(adres, linki[akt].href, pel, sizeof pel)) ksprintf(s, sizeof s, "%3d%%  %s", proc, pel);
        else ksprintf(s, sizeof s, "%3d%%  (link nieobsługiwany)", proc);
    } else
        ksprintf(s, sizeof s, "%3d%%  ↑↓ linki  Enter otwórz  ← wstecz  G adres  H start  Esc wyjście", proc);
    s[79] = 0;
    pasek(s);
}

/* ---------- składanie tekstu strony ---------- */

static int kol, tresc, puste, wciecie, pre, naglowek, grube, w_linku, glebokosc, ost_link;
static int spacja, pelna;
static u32 sl_cp[160]; static u8 sl_at[160]; static short sl_ln[160]; static int sl_n;

static u8 atr(void) {
    if (w_linku >= 0) return ATTR(JASNOTURKUSOWY, CZARNY);
    if (naglowek) return ATTR(ZOLTY, CZARNY);
    if (grube) return ATTR(BIALY, CZARNY);
    return ATTR(SZARY, CZARNY);
}

static void nowa_linia(void) {
    if (wierszy >= MAXW - 2) { pelna = 1; return; }     /* ostatni wiersz zostaje na komunikat */
    if (tresc) puste = 0; else puste++;
    memset(strona[wierszy], 0, sizeof strona[0]);
    wierszy++;
    kol = wciecie; tresc = 0;
}

static void postaw(u32 cp, u8 at, int ln) {
    if (pelna) return;
    if (kol >= SZER) nowa_linia();
    if (pelna) return;
    int w = wierszy - 1;
    strona[w][kol] = (u16)(at << 8 | vga_map(cp));
    if (ln >= 0) {
        link_t *L = &linki[ln];
        if (L->w0 < 0) { L->w0 = w; L->k0 = kol; }
        L->w1 = w; L->k1 = kol;
    }
    ost_link = ln;
    kol++; tresc = 1;
}

static void slowo_zrzuc(void) {
    if (!sl_n) return;
    int odstep = spacja && kol > wciecie;
    if (kol + sl_n + odstep > SZER && kol > wciecie) { nowa_linia(); odstep = 0; }
    if (odstep) {
        int ln = (ost_link >= 0 && ost_link == sl_ln[0]) ? ost_link : -1;
        postaw(' ', ln >= 0 ? sl_at[0] : ATTR(SZARY, CZARNY), ln);
    }
    for (int i = 0; i < sl_n; i++) postaw(sl_cp[i], sl_at[i], sl_ln[i]);
    sl_n = 0; spacja = 0;
}

static void blok(int przerwa) {
    slowo_zrzuc();
    spacja = 0;
    if (tresc) nowa_linia();
    while (puste < przerwa && !pelna) nowa_linia();
    kol = wciecie;
}

static void znak_tekstu(u32 cp) {
    if (pre) {
        slowo_zrzuc();
        if (cp == '\n') { nowa_linia(); return; }
        if (cp == '\r') return;
        if (cp == '\t') { do postaw(' ', atr(), w_linku); while (kol % 8); return; }
        postaw(cp, atr(), w_linku);
        return;
    }
    if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0) { slowo_zrzuc(); spacja = 1; return; }
    if (cp < 0x20) return;
    if (sl_n == 160) slowo_zrzuc();
    sl_cp[sl_n] = cp; sl_at[sl_n] = atr(); sl_ln[sl_n] = (short)w_linku; sl_n++;
}

static void tekst(const char *s) { while (*s) znak_tekstu(k_utf8_next(&s)); }

/* ---------- dekodowanie znaków i encji ---------- */

int kodowanie;

u32 znak_z(const u8 **pp, const u8 *k) {
    const u8 *p = *pp;
    u8 c = *p;
    if (c < 0x80) { *pp = p + 1; return c; }
    if (kodowanie == ISO2) { *pp = p + 1; return kod_iso88592[c - 0x80]; }
    if (kodowanie == CP1250) { *pp = p + 1; return kod_cp1250[c - 0x80]; }
    if (kodowanie == LATIN1) { *pp = p + 1; return c; }
    if ((c & 0xE0) == 0xC0 && p + 1 < k && (p[1] & 0xC0) == 0x80) { *pp = p + 2; return ((u32)(c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0 && p + 2 < k && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *pp = p + 3; return ((u32)(c & 0x0F) << 12) | ((u32)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && p + 3 < k) { *pp = p + 4; return '?'; }
    *pp = p + 1;
    return c;                                            /* nie-UTF-8: potraktuj jak Latin-1 */
}

static const struct { const char *n; u16 cp; } encje[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' },
    { "copy", 0xA9 }, { "reg", 0xAE }, { "deg", 0xB0 }, { "middot", 0xB7 }, { "laquo", 0xAB }, { "raquo", 0xBB },
    { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "bdquo", 0x201E }, { "rdquo", 0x201D },
    { "ldquo", 0x201C }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 }, { "euro", 0x20AC }, { "bull", 0x2022 },
    { "times", 0xD7 }, { "oacute", 0xF3 }, { "Oacute", 0xD3 }, { "eacute", 0xE9 }, { "aacute", 0xE1 },
    { "uuml", 0xFC }, { "ouml", 0xF6 }, { "auml", 0xE4 }, { "szlig", 0xDF }, { "shy", 0 }, { "zwnj", 0 },
};

u32 encja(const u8 **pp, const u8 *k) {
    const u8 *p = *pp + 1;
    char n[12]; int i = 0;
    while (p < k && *p != ';' && i < 10 && *p != '&' && *p != '<' && *p != ' ') n[i++] = (char)*p++;
    n[i] = 0;
    if (p >= k || *p != ';' || !i) { *pp += 1; return '&'; }
    u32 cp = 0xFFFFFFFF;
    if (n[0] == '#') {
        u32 v = 0;
        if (n[1] == 'x' || n[1] == 'X') {
            for (int j = 2; n[j]; j++) {
                char c = mala(n[j]);
                v = v * 16 + (u32)(c >= 'a' ? c - 'a' + 10 : c - '0');
            }
        } else for (int j = 1; n[j]; j++) v = v * 10 + (u32)(n[j] - '0');
        cp = v;
    } else {
        for (u32 j = 0; j < sizeof encje / sizeof encje[0]; j++)
            if (!k_strcmp(n, encje[j].n)) { cp = encje[j].cp; break; }
    }
    if (cp == 0xFFFFFFFF) { *pp += 1; return '&'; }
    *pp = p + 1;
    return cp;
}

/* wartość atrybutu znacznika (z rozwiniętymi encjami), np. href */
int atrybut(const u8 *s, const u8 *e, const char *nazwa, char *o, u32 max) {
    u32 n = k_strlen(nazwa);
    for (const u8 *p = s; p + n < e; p++) {
        if (p > s && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '\n' && p[-1] != '\r' && p[-1] != '"' && p[-1] != '\'') continue;
        u32 i = 0;
        while (i < n && mala((char)p[i]) == nazwa[i]) i++;
        if (i < n) continue;
        const u8 *q = p + n;
        while (q < e && *q == ' ') q++;
        if (q >= e || *q != '=') continue;
        q++;
        while (q < e && *q == ' ') q++;
        u8 cudz = 0;
        if (q < e && (*q == '"' || *q == '\'')) cudz = *q++;
        u32 d = 0;
        while (q < e && (cudz ? *q != cudz : (*q != ' ' && *q != '>')) && d + 4 < max) {
            if (*q == '&') {
                u32 cp = encja(&q, e);
                d += (u32)k_utf8_put(cp, o + d);
            } else o[d++] = (char)*q++;
        }
        o[d] = 0;
        return 1;
    }
    return 0;
}

/* ---------- HTML ---------- */

static void sklad_start(void) {
    wierszy = 0; nlink = 0; tytul[0] = 0;
    kol = tresc = wciecie = pre = naglowek = grube = glebokosc = 0;
    puste = 2; w_linku = -1; ost_link = -1; spacja = 0; pelna = 0; sl_n = 0;
    memset(strona[0], 0, sizeof strona[0]);
    wierszy = 1;
}

static void sklad_koniec(void) {
    slowo_zrzuc();
    if (pelna) {
        memset(strona[wierszy], 0, sizeof strona[0]);
        const char *m = "[ Strona jest bardzo długa - pokazano jej początek ]";
        for (int k = 0; *m && k < SZER; k++) strona[wierszy][k] = (u16)(ATTR(ZOLTY, CZARNY) << 8 | vga_map(k_utf8_next(&m)));
        wierszy++;
        return;
    }
    while (wierszy > 1) {
        int pusty = 1;
        for (int k = 0; k < SZER; k++) if (strona[wierszy - 1][k] && (u8)strona[wierszy - 1][k] != ' ') { pusty = 0; break; }
        if (!pusty) break;
        wierszy--;
    }
}

static int jest(const char *n, const char *const *lista) {
    for (; *lista; lista++) if (!k_strcmp(n, *lista)) return 1;
    return 0;
}

static const char *const BLOKI[] = { "div", "section", "article", "main", "header", "footer", "nav", "aside", "form",
    "table", "tr", "center", "figure", "figcaption", "address", "fieldset", "details", "summary", "dl", "dt", "dd",
    "caption", "tbody", "thead", "tfoot", "legend", "menu", 0 };

static void znacznik(const char *n, int zamyk, const u8 *s, const u8 *e) {
    char w[ADR];
    if (!k_strcmp(n, "p")) blok(1);
    else if (!k_strcmp(n, "br")) { slowo_zrzuc(); spacja = 0; nowa_linia(); }
    else if (jest(n, BLOKI)) blok(0);
    else if (n[0] == 'h' && n[1] >= '1' && n[1] <= '6' && !n[2]) { blok(1); naglowek = !zamyk; }
    else if (!k_strcmp(n, "b") || !k_strcmp(n, "strong")) { grube += zamyk ? -1 : 1; if (grube < 0) grube = 0; }
    else if (!k_strcmp(n, "blockquote")) { blok(1); wciecie += zamyk ? -4 : 4; if (wciecie < 0) wciecie = 0; kol = wciecie; }
    else if (!k_strcmp(n, "ul") || !k_strcmp(n, "ol")) {
        blok(glebokosc == 0 && !zamyk ? 1 : 0);
        glebokosc += zamyk ? -1 : 1;
        if (glebokosc < 0) glebokosc = 0;
        wciecie = glebokosc * 2 > 16 ? 16 : glebokosc * 2;
        kol = wciecie;
    } else if (!k_strcmp(n, "li") && !zamyk) {
        blok(0);
        if (wciecie < 2) wciecie = 2;
        kol = wciecie - 2;
        postaw(0x2022, ATTR(ZOLTY, CZARNY), -1);
        postaw(' ', ATTR(SZARY, CZARNY), -1);
    } else if (!k_strcmp(n, "a")) {
        if (w_linku >= 0) {                              /* zamknij poprzedni link */
            if (linki[w_linku].w0 < 0 && !sl_n) tekst("[link]");
            slowo_zrzuc();
            w_linku = -1;
        }
        if (!zamyk && nlink < MAXL && atrybut(s, e, "href", w, sizeof w)) {
            link_t *L = &linki[nlink];
            k_strncpy(L->href, w, sizeof L->href);
            L->w0 = -1;
            w_linku = nlink++;
        }
    } else if (!k_strcmp(n, "img")) {
        if (atrybut(s, e, "alt", w, 80) && w[0]) {
            int stary = grube; grube = 0;
            znak_tekstu('['); tekst(w); znak_tekstu(']');
            grube = stary;
        } else if (w_linku >= 0) tekst("[obraz]");
    } else if (!k_strcmp(n, "hr")) {
        blok(0);
        for (int i = 0; i < SZER; i++) postaw(0x2500, ATTR(CIEMNOSZARY, CZARNY), -1);
        nowa_linia();
    } else if (!k_strcmp(n, "pre")) { blok(0); pre = !zamyk; }
    else if ((!k_strcmp(n, "td") || !k_strcmp(n, "th")) && !zamyk) {
        slowo_zrzuc();
        if (kol > wciecie) { postaw(' ', ATTR(SZARY, CZARNY), -1); postaw(' ', ATTR(SZARY, CZARNY), -1); }
        spacja = 0;
    } else if (!k_strcmp(n, "input") && !zamyk) {
        char typ[16] = "";
        atrybut(s, e, "type", typ, sizeof typ);
        if (!k_strcmp(typ, "hidden") || !k_strcmp(typ, "checkbox") || !k_strcmp(typ, "radio")) return;
        if ((!k_strcmp(typ, "submit") || !k_strcmp(typ, "button")) && atrybut(s, e, "value", w, 40)) {
            znak_tekstu('['); tekst(w); znak_tekstu(']');
        } else tekst("[______]");
    }
}

static void renderuj_html(const u8 *p, const u8 *k) {
    sklad_start();
    while (p < k && !pelna) {
        if (*p == '<') {
            const u8 *s = p + 1;
            if (s + 3 <= k && s[0] == '!' && s[1] == '-' && s[2] == '-') {
                const u8 *z = szukaj_ci(s, k, "-->");
                p = z ? z + 3 : k;
                continue;
            }
            int zamyk = 0;
            if (s < k && *s == '/') { zamyk = 1; s++; }
            char n[16]; int i = 0;
            while (s < k && i < 15 && ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9')))
                n[i++] = mala((char)*s++);
            n[i] = 0;
            if (!i && !(s < k && (*s == '!' || *s == '?'))) { znak_tekstu('<'); p++; continue; }
            const u8 *e = s; u8 cudz = 0;
            while (e < k) {
                if (cudz) { if (*e == cudz) cudz = 0; }
                else if (*e == '"' || *e == '\'') cudz = *e;
                else if (*e == '>') break;
                e++;
            }
            p = e < k ? e + 1 : k;
            if (!i) continue;                                       /* <!DOCTYPE>, <?xml?> */
            if (!zamyk && (!k_strcmp(n, "script") || !k_strcmp(n, "style") || !k_strcmp(n, "svg") ||
                           !k_strcmp(n, "template") || !k_strcmp(n, "select"))) {
                char wz[16]; ksprintf(wz, sizeof wz, "</%s", n);
                const u8 *z = szukaj_ci(p, k, wz);
                if (!k_strcmp(n, "select")) tekst("[lista]");
                p = z ? z : k;
                continue;
            }
            if (!zamyk && !k_strcmp(n, "title")) {
                const u8 *z = szukaj_ci(p, k, "</title");
                const u8 *q = p; u32 d = 0; int sp = 0;
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
        if (cp) znak_tekstu(cp);
    }
    if (w_linku >= 0 && linki[w_linku].w0 < 0 && !sl_n) tekst("[link]");
    sklad_koniec();
}

static void renderuj_tekst(const u8 *p, const u8 *k) {
    sklad_start();
    pre = 1;
    while (p < k && !pelna) znak_tekstu(znak_z(&p, k));
    sklad_koniec();
}

static void renderuj_napis(const char *html) {
    kodowanie = UTF8;
    renderuj_html((const u8 *)html, (const u8 *)html + k_strlen(html));
}

/* ---------- strony wbudowane ---------- */

const char *STRONA_STARTOWA =
    "<title>AmenOS - strona startowa</title>"
    "<h1>Przeglądarka AmenOS</h1>"
    "<p>Naciśnij <b>G</b>, aby wpisać adres strony albo szukane słowa (wyszukiwarka FrogFind).</p>"
    "<h2>Kościół w sieci</h2><ul>"
    "<li><a href=\"https://www.vaticannews.va/pl.html\">Vatican News po polsku</a>"
    "<li><a href=\"https://opoka.org.pl\">Opoka - portal katolicki</a>"
    "<li><a href=\"https://www.niedziela.pl\">Tygodnik Katolicki Niedziela</a>"
    "<li><a href=\"https://episkopat.pl\">Konferencja Episkopatu Polski</a>"
    "</ul><h2>Strony przyjazne starszym komputerom</h2><ul>"
    "<li><a href=\"http://frogfind.com\">FrogFind - wyszukiwarka</a>"
    "<li><a href=\"http://68k.news\">68k.news - wiadomości ze świata</a>"
    "<li><a href=\"http://info.cern.ch\">Pierwsza strona WWW na świecie (CERN, 1991)</a>"
    "</ul><h2>Sterowanie</h2><ul>"
    "<li><b>Strzałki góra/dół</b> - poprzedni/następny link"
    "<li><b>Enter</b> lub <b>strzałka w prawo</b> - otwórz link"
    "<li><b>Strzałka w lewo</b> lub <b>Backspace</b> - wstecz"
    "<li><b>PgUp/PgDn, spacja</b> - przewijanie, <b>Home/End</b> - początek/koniec"
    "<li><b>G</b> - adres lub wyszukiwanie, <b>R</b> - odśwież, <b>H</b> - strona startowa, <b>Esc</b> - wyjście"
    "</ul><p>Strony szyfrowane (https://) są otwierane przez serwis FrogFind, który pobiera je "
    "i zamienia na prosty tekst. Formularze i JavaScript nie są obsługiwane.</p>";

static void strona_bledu(const char *naglowek_bledu, const char *opis) {
    static char h[1200];
    ksprintf(h, sizeof h, "<title>Błąd</title><h1>%s</h1><p>%s</p><p><a href=\"" START "\">Strona startowa</a></p>",
             naglowek_bledu, opis);
    renderuj_napis(h);
}

/* ---------- HTTP ---------- */

static int naglowek_http(const char *h, const char *koniec, const char *nazwa, char *o, u32 max) {
    u32 n = k_strlen(nazwa);
    for (const char *p = h; p < koniec; p++) {
        if ((p == h || p[-1] == '\n') && zaczyna(p, nazwa) && p[n] == ':') {
            p += n + 1;
            while (*p == ' ') p++;
            u32 d = 0;
            while (p < koniec && *p != '\r' && *p != '\n' && d + 1 < max) o[d++] = *p++;
            o[d] = 0;
            return 1;
        }
    }
    return 0;
}

static u32 chunked_dekoduj(u8 *b, u32 dl) {
    u32 r = 0, w = 0;
    while (r < dl) {
        u32 n = 0; int cyfr = 0;
        while (r < dl) {
            char c = mala((char)b[r]);
            if (c >= '0' && c <= '9') n = n * 16 + (u32)(c - '0');
            else if (c >= 'a' && c <= 'f') n = n * 16 + (u32)(c - 'a' + 10);
            else break;
            r++; cyfr++;
        }
        while (r < dl && b[r] != '\n') r++;
        r++;
        if (!cyfr || n == 0) break;
        if (r + n > dl) n = dl - r;
        for (u32 i = 0; i < n; i++) b[w++] = b[r + i];
        r += n + 2;
    }
    return w;
}

int ustal_kodowanie(const char *typ, const u8 *cialo, u32 dl) {
    char cs[24] = "";
    const char *c = 0;
    for (const char *p = typ; *p; p++) if (zaczyna(p, "charset=")) { c = p + 8; break; }
    if (!c) {
        const u8 *z = szukaj_ci(cialo, cialo + (dl < 4000 ? dl : 4000), "charset=");
        if (z) c = (const char *)z + 8;
    }
    if (c) {
        while (*c == '"' || *c == '\'') c++;
        int i = 0;
        while (c[i] && c[i] != '"' && c[i] != '\'' && c[i] != ';' && c[i] != ' ' && c[i] != '>' && i < 23) { cs[i] = mala(c[i]); i++; }
        cs[i] = 0;
    }
    if (zaczyna(cs, "iso-8859-2") || zaczyna(cs, "latin2")) return ISO2;
    if (zaczyna(cs, "windows-1250") || zaczyna(cs, "cp1250")) return CP1250;
    if (zaczyna(cs, "iso-8859-1") || zaczyna(cs, "windows-1252") || zaczyna(cs, "us-ascii")) return LATIN1;
    return UTF8;
}

/* ---------- wspólne pobieranie HTTP (także dla przeglądarki graficznej) ---------- */

static int pob_zajete;

int www_pobierz(const char *url_we, u8 *bufor, u32 rozmiar, www_odp_t *o, int (*postep)(const char *msg)) {
    char url[ADR], tmp[ADR], msg[120];
    memset(o, 0, sizeof *o);
    k_strncpy(url, url_we, sizeof url);
    if (pob_zajete) { k_strncpy(o->blad_tytul, "Sieć zajęta", sizeof o->blad_tytul);
                      k_strncpy(o->blad, "Trwa inne pobieranie - spróbuj za chwilę.", sizeof o->blad); return 0; }
    if (!siec_gotowa) {
        if (postep) postep("Uruchamianie sieci: karta sieciowa i DHCP...");
        int e = siec_start();
        if (e) { k_strncpy(o->blad_tytul, "Brak sieci", sizeof o->blad_tytul); k_strncpy(o->blad, siec_blad(e), sizeof o->blad); return 0; }
    }
    pob_zajete = 1;
    int wynik = 0;
    for (int przekier = 0; przekier < 6; przekier++) {
        if (zaczyna(url, "https://")) { przez_frogfind(url, tmp, sizeof tmp); k_strncpy(url, tmp, sizeof url); }
        url_t u;
        k_strncpy(o->url, url, sizeof o->url);
        if (!url_rozbierz(url, &u)) { k_strncpy(o->blad_tytul, "Nieprawidłowy adres", sizeof o->blad_tytul); k_strncpy(o->blad, url, sizeof o->blad); goto koniec; }
        ksprintf(msg, sizeof msg, "Szukanie serwera %s...", u.host);
        if (postep && postep(msg)) goto przerwano;
        u32 ip;
        if (!dns_zapytaj(u.host, &ip)) {
            k_strncpy(o->blad_tytul, "Nie znaleziono strony", sizeof o->blad_tytul);
            ksprintf(o->blad, sizeof o->blad, "Nie znaleziono serwera <b>%s</b>. Sprawdź adres.", u.host);
            goto koniec;
        }
        char ipt[16]; ip_tekst(ip, ipt);
        ksprintf(msg, sizeof msg, "Łączenie z %s (%s)...", u.host, ipt);
        if (postep && postep(msg)) goto przerwano;
        if (!tcp_polacz(ip, u.port, bufor, rozmiar - 1)) {
            k_strncpy(o->blad_tytul, "Brak połączenia", sizeof o->blad_tytul);
            ksprintf(o->blad, sizeof o->blad, "Serwer %s (%s) nie odpowiada.", u.host, ipt);
            goto koniec;
        }
        static char zap[ADR + 300];
        ksprintf(zap, sizeof zap, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: AmenOS/" AMENOS_WERSJA
                 " (x86)\r\nAccept: text/html, text/plain, image/*\r\nAccept-Charset: utf-8\r\n"
                 "Accept-Language: pl, en\r\nConnection: close\r\n\r\n", u.sciezka, u.host);
        if (!tcp_wyslij(zap, k_strlen(zap))) {
            tcp_zamknij();
            k_strncpy(o->blad_tytul, "Błąd połączenia", sizeof o->blad_tytul);
            k_strncpy(o->blad, "Nie udało się wysłać zapytania.", sizeof o->blad);
            goto koniec;
        }
        u32 t_pasek = 0;
        int przerw = 0;
        while (!tcp_zakonczone() && tcp_cisza_ms() < 15000 && tcp_odebrano() < rozmiar - 1) {
            siec_odbieraj();
            if (postep) {
                const char *m = 0;
                if (zegar_ms() - t_pasek > 250) {
                    t_pasek = zegar_ms();
                    ksprintf(msg, sizeof msg, "Pobieranie %s...  %u KB   (Esc - przerwij)", u.host, (int)(tcp_odebrano() / 1024));
                    m = msg;
                }
                if (postep(m)) { przerw = 1; break; }
            }
        }
        tcp_zamknij();
        u32 dl = tcp_odebrano();
        bufor[dl] = 0;
        if (przerw && !dl) goto przerwano;

        const u8 *koniec_nagl = szukaj_ci(bufor, bufor + dl, "\r\n\r\n");
        u32 start = 4;
        if (!koniec_nagl) { koniec_nagl = szukaj_ci(bufor, bufor + dl, "\n\n"); start = 2; }
        if (!koniec_nagl || !zaczyna((const char *)bufor, "HTTP/")) {
            k_strncpy(o->blad_tytul, "Błędna odpowiedź", sizeof o->blad_tytul);
            k_strncpy(o->blad, "Serwer odpowiedział w nieznany sposób.", sizeof o->blad);
            goto koniec;
        }
        const char *h = (const char *)bufor, *hk = (const char *)koniec_nagl;
        int kod = 0;
        const char *sp = h; while (sp < hk && *sp != ' ') sp++;
        for (sp++; *sp >= '0' && *sp <= '9'; sp++) kod = kod * 10 + (*sp - '0');
        char lok[ADR];
        if (kod >= 300 && kod < 400 && naglowek_http(h, hk, "location", lok, sizeof lok)) {
            if (zaczyna(lok, "https://")) k_strncpy(tmp, lok, sizeof tmp);
            else if (!url_pelny(url, lok, tmp, sizeof tmp)) {
                k_strncpy(o->blad_tytul, "Błędne przekierowanie", sizeof o->blad_tytul); k_strncpy(o->blad, lok, sizeof o->blad); goto koniec;
            }
            k_strncpy(url, tmp, sizeof url);
            continue;
        }
        o->cialo = (u8 *)koniec_nagl + start;
        o->dl = dl - (u32)(o->cialo - bufor);
        char te[32] = "";
        if (naglowek_http(h, hk, "transfer-encoding", te, sizeof te) && zaczyna(te, "chunked")) o->dl = chunked_dekoduj(o->cialo, o->dl);
        naglowek_http(h, hk, "content-type", o->typ, sizeof o->typ);
        o->kod = kod;
        o->cialo[o->dl] = 0;
        wynik = 1;
        goto koniec;
    }
    k_strncpy(o->blad_tytul, "Za dużo przekierowań", sizeof o->blad_tytul);
    k_strncpy(o->blad, url, sizeof o->blad);
    goto koniec;
przerwano:
    k_strncpy(o->blad_tytul, "Przerwano", sizeof o->blad_tytul);
    k_strncpy(o->blad, "Pobieranie zostało przerwane.", sizeof o->blad);
koniec:
    pob_zajete = 0;
    return wynik;
}

static int postep_tekstowy(const char *m) {
    if (m) pasek(m);
    return kbd_poll() == KEY_ESC;
}

/* pobiera stronę i ją składa (przeglądarka tekstowa) */
static int pobierz_i_pokaz(const char *url_we) {
    static www_odp_t o;
    char msg[160];
    int ok = www_pobierz(url_we, dane, sizeof dane, &o, postep_tekstowy);
    k_strncpy(adres, o.url[0] ? o.url : url_we, sizeof adres);
    if (!ok) { strona_bledu(o.blad_tytul, o.blad); return 1; }
    dane_dl = o.dl;
    kodowanie = ustal_kodowanie(o.typ, o.cialo, o.dl);
    if (!o.typ[0] || szukaj_ci((const u8 *)o.typ, (const u8 *)o.typ + k_strlen(o.typ), "html")) renderuj_html(o.cialo, o.cialo + o.dl);
    else if (zaczyna(o.typ, "text/")) renderuj_tekst(o.cialo, o.cialo + o.dl);
    else {
        ksprintf(msg, sizeof msg, "Ten rodzaj pliku (%s) nie może zostać wyświetlony.", o.typ);
        strona_bledu("Plik nie jest stroną", msg);
    }
    if (o.kod >= 400 && !tytul[0]) ksprintf(tytul, sizeof tytul, "Błąd HTTP %d", o.kod);
    return 1;
}

static void otworz(const char *url) {
    if (!k_strcmp(url, START)) { renderuj_napis(STRONA_STARTOWA); k_strncpy(adres, START, sizeof adres); }
    else pobierz_i_pokaz(url);
    przewin = 0;
    akt = (nlink > 0 && linki[0].w0 >= 0 && linki[0].w0 < WIDOK) ? 0 : -1;
}

static void idz(const char *url) {
    if (hist_n == 24) { memcpy(hist, hist + 1, sizeof hist[0] * 23); hist_n--; }
    k_strncpy(hist[hist_n].url, adres, ADR);
    hist[hist_n].przewin = przewin; hist[hist_n].akt = akt;
    hist_n++;
    otworz(url);
}

static void wstecz(void) {
    if (!hist_n) return;
    hist_n--;
    otworz(hist[hist_n].url);
    przewin = hist[hist_n].przewin;
    if (przewin > wierszy - 1) przewin = 0;
    akt = hist[hist_n].akt < nlink ? hist[hist_n].akt : -1;
}

/* wpisany tekst: adres strony albo wyszukiwanie */
void wpisany_adres(const char *s, char *o, u32 max) {
    while (*s == ' ') s++;
    int kropka = 0, odstep = 0;
    for (const char *p = s; *p; p++) { if (*p == '.') kropka = 1; if (*p == ' ') odstep = 1; }
    if (zaczyna(s, "http://") || zaczyna(s, "https://") || !k_strcmp(s, START)) k_strncpy(o, s, max);
    else if (kropka && !odstep) { k_strncpy(o, "http://", max); dopisz(o, max, s); }
    else { char q[ADR]; url_koduj(s, q, sizeof q); ksprintf(o, max, FROGFIND "/?q=%s", q); }
}

static void popraw_aktywny(void) {
    if (akt >= 0 && akt < nlink && linki[akt].w0 >= przewin && linki[akt].w0 < przewin + WIDOK) return;
    akt = -1;
    for (int i = 0; i < nlink; i++)
        if (linki[i].w0 >= przewin && linki[i].w0 < przewin + WIDOK) { akt = i; break; }
}

void przegladarka(const char *start) {
    char url[ADR];
    hist_n = 0;
    adres[0] = 0;
    vga_fill(0, 0, VGA_W, VGA_H, ' ', ATTR(SZARY, CZARNY));
    if (start && *start) wpisany_adres(start, url, sizeof url);
    else k_strncpy(url, START, sizeof url);
    otworz(url);
    for (;;) {
        rysuj();
        int k = kbd_getkey();
        int max_przew = wierszy > WIDOK ? wierszy - WIDOK : 0;
        if (k == KEY_ESC || k == 'q' || k == 'Q') break;
        else if (k == KEY_DOWN || k == '\t') {
            int n = akt + 1;
            while (n < nlink && linki[n].w0 < przewin) n++;
            if (n < nlink && linki[n].w0 >= 0 && linki[n].w0 < przewin + WIDOK) akt = n;
            else if (przewin < max_przew) {
                przewin += WIDOK / 2; if (przewin > max_przew) przewin = max_przew;
                if (n < nlink && linki[n].w0 < przewin + WIDOK) akt = n; else popraw_aktywny();
            }
        } else if (k == KEY_UP) {
            int n = akt - 1;
            while (n >= 0 && linki[n].w0 >= przewin + WIDOK) n--;
            if (akt >= 0 && n >= 0 && linki[n].w0 >= przewin) akt = n;
            else if (przewin > 0) {
                przewin -= WIDOK / 2; if (przewin < 0) przewin = 0;
                if (n >= 0 && linki[n].w0 >= przewin) akt = n; else popraw_aktywny();
            }
        } else if (k == KEY_PGDN || k == ' ') { przewin += WIDOK - 1; if (przewin > max_przew) przewin = max_przew; popraw_aktywny(); }
        else if (k == KEY_PGUP) { przewin -= WIDOK - 1; if (przewin < 0) przewin = 0; popraw_aktywny(); }
        else if (k == KEY_HOME) { przewin = 0; popraw_aktywny(); }
        else if (k == KEY_END) { przewin = max_przew; popraw_aktywny(); }
        else if (k == KEY_ENTER || k == KEY_RIGHT) {
            if (akt >= 0 && akt < nlink) {
                if (url_pelny(adres, linki[akt].href, url, sizeof url)) idz(url);
            }
        } else if (k == KEY_LEFT || k == KEY_BKSP) wstecz();
        else if (k == 'r' || k == 'R') { int p = przewin; otworz(adres); przewin = p > max_przew ? 0 : p; popraw_aktywny(); }
        else if (k == 'h' || k == 'H') idz(START);
        else if (k == 'g' || k == 'G') {
            char wej[ADR];
            vga_fill(0, 1, VGA_W, 1, ' ', ATTR(CZARNY, BIALY));
            vga_puts_at(0, 1, " Adres:", ATTR(NIEBIESKI, BIALY));
            pasek("Wpisz adres (np. info.cern.ch) albo szukane słowa i naciśnij Enter. Esc - wyczyść.");
            vga_color(CZARNY, BIALY);
            vga_goto(8, 1);
            vga_cursor(1);
            int n = kbd_readline(wej, 70);
            vga_cursor(0);
            if (n > 0) { wpisany_adres(wej, url, sizeof url); idz(url); }
        }
    }
    vga_color(SZARY, CZARNY);
    vga_clear();
    vga_cursor(1);
}

/* ---------- polecenia powłoki: siec, ping ---------- */

void cmd_siec(char *p) {
    char *a = k_token(&p);
    char f[16];
    if (a) k_fold_pl(f, a, sizeof f);
    if (a && !k_strcmp(f, "dns")) {
        char *v = k_token(&p); u32 ip;
        if (!v || !ip_z_tekstu(v, &ip)) { kprintf("Użycie: siec dns A.B.C.D\n"); return; }
        dns_ip = dns_wlasny = ip;
        kprintf("Serwer DNS: %s\n", v);
        return;
    }
    if (!siec_gotowa) {
        kprintf("Uruchamianie sieci (karta sieciowa, DHCP)...\n");
        int e = siec_start();
        if (e) { vga_color(JASNOCZERWONY, CZARNY); kprintf("%s\n", siec_blad(e)); vga_color(SZARY, CZARNY); return; }
    }
    char t[4][16];
    ip_tekst(moje_ip, t[0]); ip_tekst(maska, t[1]); ip_tekst(brama, t[2]); ip_tekst(dns_ip, t[3]);
    vga_color(ZOLTY, CZARNY); kprintf("\n  Sieć\n");
    vga_color(SZARY, CZARNY);
    kprintf("  Karta:   %s\n", karta_nazwa());
    kprintf("  MAC:     %02x:%02x:%02x:%02x:%02x:%02x\n", karta_mac[0], karta_mac[1], karta_mac[2], karta_mac[3], karta_mac[4], karta_mac[5]);
    kprintf("  Adres:   %s / %s\n  Brama:   %s\n  DNS:     %s\n\n", t[0], t[1], t[2], t[3]);
}

void cmd_ping(char *arg) {
    if (!arg) { kprintf("Użycie: ping ADRES   (np. ping 10.0.2.2 albo ping example.com)\n"); return; }
    if (!siec_gotowa) {
        kprintf("Uruchamianie sieci...\n");
        int e = siec_start();
        if (e) { kprintf("%s\n", siec_blad(e)); return; }
    }
    u32 ip;
    if (!dns_zapytaj(arg, &ip)) { kprintf("Nie znaleziono: %s\n", arg); return; }
    char t[16]; ip_tekst(ip, t);
    kprintf("Ping %s (%s):\n", arg, t);
    for (int i = 0; i < 4; i++) {
        u32 ms;
        if (ping(ip, &ms)) kprintf("  odpowiedź od %s: czas %u ms\n", t, (int)ms);
        else kprintf("  brak odpowiedzi\n");
        if (kbd_poll() == KEY_ESC) break;
        czekaj_ms(500);
    }
}
