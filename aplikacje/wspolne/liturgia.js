/* AmenOS - kalendarz liturgiczny Kościoła rzymskokatolickiego w Polsce (uproszczony).
 * Przeniesiony 1:1 z jądra AmenOS (jadro/src/liturgia.c). */
"use strict";

const LIT = (() => {
  const DNI = ["niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota"];
  const DNI_W = ["Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota"];
  const MIES_DOP = ["", "stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca", "lipca", "sierpnia", "września", "października", "listopada", "grudnia"];
  const MIES = ["", "Styczeń", "Luty", "Marzec", "Kwiecień", "Maj", "Czerwiec", "Lipiec", "Sierpień", "Wrzesień", "Październik", "Listopad", "Grudzień"];
  const KOLORY = ["zielony", "fioletowy", "biały", "czerwony", "różowy"];
  const ZIELONY = 0, FIOLET = 1, BIALY = 2, CZERWONY = 3, ROZOWY = 4;
  const R_PRZYWILEJ = 1, R_UROCZ = 2, R_NIEDZ = 3, R_SWIETO = 4, R_UPRZYW = 5, R_WSPOM = 6, R_POWSZ = 7;
  const dz = (a, b) => Math.trunc(a / b);

  /* arytmetyka dat (H. Hinnant) */
  function dniOdEpoki(y, m, d) {
    y -= m <= 2 ? 1 : 0;
    const era = dz(y >= 0 ? y : y - 399, 400), yoe = y - era * 400;
    const doy = dz(153 * (m + (m > 2 ? -3 : 9)) + 2, 5) + d - 1;
    const doe = yoe * 365 + dz(yoe, 4) - dz(yoe, 100) + doy;
    return era * 146097 + doe - 719468;
  }
  function dataZDni(z) {
    z += 719468;
    const era = dz(z >= 0 ? z : z - 146096, 146097), doe = z - era * 146097;
    const yoe = dz(doe - dz(doe, 1460) + dz(doe, 36524) - dz(doe, 146096), 365);
    const y = yoe + era * 400, doy = doe - (365 * yoe + dz(yoe, 4) - dz(yoe, 100));
    const mp = dz(5 * doy + 2, 153), d = doy - dz(153 * mp + 2, 5) + 1, m = mp < 10 ? mp + 3 : mp - 9;
    return { r: y + (m <= 2 ? 1 : 0), m, d };
  }
  function dzienTygodnia(z) { const w = (z + 4) % 7; return w < 0 ? w + 7 : w; }
  function dniWMiesiacu(r, m) {
    const t = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    return m === 2 && ((r % 4 === 0 && r % 100 !== 0) || r % 400 === 0) ? 29 : t[m];
  }
  function wielkanoc(y) {
    const a = y % 19, b = dz(y, 100), c = y % 100, d = dz(b, 4), e = b % 4;
    const f = dz(b + 8, 25), g = dz(b - f + 1, 3), h = (19 * a + b - d - g + 15) % 30;
    const i = dz(c, 4), k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7, m = dz(a + 11 * h + 22 * l, 451);
    return dniOdEpoki(y, dz(h + l - 7 * m + 114, 31), (h + l - 7 * m + 114) % 31 + 1);
  }
  function adwent1(rok) { const bn = dniOdEpoki(rok, 12, 25), w = dzienTygodnia(bn); return bn - (w ? w : 7) - 21; }
  function rzymska(n) {
    const v = [10, 9, 5, 4, 1], s = ["X", "IX", "V", "IV", "I"];
    let o = "";
    for (let i = 0; i < 5; i++) while (n >= v[i]) { o += s[i]; n -= v[i]; }
    return o;
  }

  const B = BIALY, C = CZERWONY;
  const U = (m, d, k, n) => ({ m, d, ranga: R_UROCZ, kolor: k, panskie: 0, nazwa: "Uroczystość " + n });
  const UP = (m, d, k, n) => ({ m, d, ranga: R_UROCZ, kolor: k, panskie: 1, nazwa: "Uroczystość " + n });
  const S = (m, d, k, n) => ({ m, d, ranga: R_SWIETO, kolor: k, panskie: 0, nazwa: "Święto " + n });
  const SP = (m, d, k, n) => ({ m, d, ranga: R_SWIETO, kolor: k, panskie: 1, nazwa: "Święto " + n });
  const W = (m, d, k, n) => ({ m, d, ranga: R_WSPOM, kolor: k, panskie: 0, nazwa: "Wspomnienie " + n });
  const OBCHODY = [
    U(1, 1, B, "Świętej Bożej Rodzicielki Maryi"), W(1, 2, B, "śś. Bazylego Wielkiego i Grzegorza z Nazjanzu"),
    UP(1, 6, B, "Objawienia Pańskiego (Trzech Króli)"), W(1, 17, B, "św. Antoniego, opata"),
    W(1, 21, C, "św. Agnieszki, dziewicy i męczennicy"), W(1, 24, B, "św. Franciszka Salezego"),
    S(1, 25, B, "Nawrócenia św. Pawła Apostoła"), W(1, 26, B, "śś. Tymoteusza i Tytusa, biskupów"),
    W(1, 28, B, "św. Tomasza z Akwinu"), W(1, 31, B, "św. Jana Bosko"),
    SP(2, 2, B, "Ofiarowania Pańskiego (MB Gromnicznej)"), W(2, 5, C, "św. Agaty, dziewicy i męczennicy"),
    W(2, 10, B, "św. Scholastyki, dziewicy"), S(2, 14, B, "śś. Cyryla i Metodego, patronów Europy"),
    S(2, 22, B, "Katedry św. Piotra Apostoła"), U(3, 19, B, "św. Józefa, Oblubieńca NMP"),
    UP(3, 25, B, "Zwiastowania Pańskiego"), U(4, 23, C, "św. Wojciecha, głównego patrona Polski"),
    S(4, 25, C, "św. Marka, Ewangelisty"), S(4, 29, B, "św. Katarzyny Sieneńskiej, patronki Europy"),
    U(5, 3, B, "NMP Królowej Polski"), S(5, 6, C, "śś. Apostołów Filipa i Jakuba"),
    U(5, 8, C, "św. Stanisława, głównego patrona Polski"), S(5, 14, C, "św. Macieja Apostoła"),
    S(5, 16, C, "św. Andrzeja Boboli, patrona Polski"), S(5, 31, B, "Nawiedzenia NMP"),
    W(6, 13, B, "św. Antoniego z Padwy"), U(6, 24, B, "Narodzenia św. Jana Chrzciciela"),
    U(6, 29, C, "śś. Apostołów Piotra i Pawła"), S(7, 3, C, "św. Tomasza Apostoła"),
    S(7, 11, B, "św. Benedykta, opata, patrona Europy"), S(7, 22, B, "św. Marii Magdaleny"),
    S(7, 23, B, "św. Brygidy, patronki Europy"), S(7, 25, C, "św. Jakuba Apostoła"),
    W(7, 26, B, "śś. Joachima i Anny, rodziców NMP"), W(7, 29, B, "śś. Marty, Marii i Łazarza"),
    W(7, 31, B, "św. Ignacego z Loyoli"), SP(8, 6, B, "Przemienienia Pańskiego"),
    W(8, 8, B, "św. Dominika, prezbitera"), S(8, 9, C, "św. Teresy Benedykty od Krzyża (Edyty Stein)"),
    S(8, 10, C, "św. Wawrzyńca, diakona i męczennika"), W(8, 14, C, "św. Maksymiliana Marii Kolbego"),
    U(8, 15, B, "Wniebowzięcia NMP"), W(8, 22, B, "NMP Królowej"), S(8, 24, C, "św. Bartłomieja Apostoła"),
    U(8, 26, B, "NMP Częstochowskiej"), W(8, 27, B, "św. Moniki"),
    W(8, 28, B, "św. Augustyna, biskupa i doktora Kościoła"), W(8, 29, C, "męczeństwa św. Jana Chrzciciela"),
    S(9, 8, B, "Narodzenia NMP"), SP(9, 14, C, "Podwyższenia Krzyża Świętego"), W(9, 15, B, "NMP Bolesnej"),
    S(9, 18, B, "św. Stanisława Kostki, patrona Polski"), S(9, 21, C, "św. Mateusza, Apostoła i Ewangelisty"),
    S(9, 29, B, "śś. Archaniołów Michała, Gabriela i Rafała"), W(9, 30, B, "św. Hieronima"),
    W(10, 1, B, "św. Teresy od Dzieciątka Jezus"), W(10, 2, B, "Świętych Aniołów Stróżów"),
    W(10, 4, B, "św. Franciszka z Asyżu"), W(10, 7, B, "NMP Różańcowej"), W(10, 15, B, "św. Teresy od Jezusa"),
    S(10, 18, C, "św. Łukasza Ewangelisty"), W(10, 22, B, "św. Jana Pawła II, papieża"),
    S(10, 28, C, "śś. Apostołów Szymona i Judy Tadeusza"), U(11, 1, B, "Wszystkich Świętych"),
    { m: 11, d: 2, ranga: R_UROCZ, kolor: FIOLET, panskie: 0, nazwa: "Wspomnienie wszystkich wiernych zmarłych" },
    W(11, 4, B, "św. Karola Boromeusza"), SP(11, 9, B, "rocznicy poświęcenia Bazyliki Laterańskiej"),
    W(11, 10, B, "św. Leona Wielkiego"), W(11, 11, B, "św. Marcina z Tours"),
    W(11, 12, C, "św. Jozafata, biskupa i męczennika"), W(11, 13, C, "Pierwszych Męczenników Polski"),
    W(11, 17, B, "św. Elżbiety Węgierskiej"), W(11, 21, B, "Ofiarowania NMP"),
    W(11, 22, C, "św. Cecylii, dziewicy i męczennicy"), W(11, 24, C, "śś. Andrzeja Dung-Lac i Towarzyszy"),
    S(11, 30, C, "św. Andrzeja Apostoła"), W(12, 3, B, "św. Franciszka Ksawerego"),
    U(12, 8, B, "Niepokalanego Poczęcia NMP"), W(12, 13, C, "św. Łucji, dziewicy i męczennicy"),
    W(12, 14, B, "św. Jana od Krzyża"), S(12, 26, C, "św. Szczepana, pierwszego męczennika"),
    S(12, 27, B, "św. Jana, Apostoła i Ewangelisty"), S(12, 28, C, "Świętych Młodzianków, męczenników"),
  ];
  const szukajObchodu = (m, d) => OBCHODY.find(o => o.m === m && o.d === d) || null;

  function ustaw(o, nazwa, kolor, ranga) {
    o.uwaga = o.ranga >= R_UPRZYW ? o.nazwa : "zamiast: " + o.nazwa;
    o.nazwa = nazwa; o.kolor = kolor; o.ranga = ranga;
  }

  function sezon(r, m, d) {
    const o = { nazwa: "", uwaga: "", okres: "", kolor: ZIELONY, ranga: R_POWSZ };
    const D = dniOdEpoki(r, m, d), dow = dzienTygodnia(D), s = D - dow;
    const E = wielkanoc(r), popiel = E - 46, palm = E - 7, wczw = E - 3, piecdz = E + 49;
    const adw = adwent1(r), krol = adw - 7, bn = dniOdEpoki(r, 12, 25);
    const jan6 = dniOdEpoki(r, 1, 6), chrzest = jan6 + (7 - dzienTygodnia(jan6));
    const dt = DNI_W[dow];
    const rokLit = D >= adw ? r + 1 : r;
    o.cyklNiedz = "CAB"[rokLit % 3];
    o.cyklPowsz = rokLit % 2 ? 1 : 2;

    if (D >= adw && D < bn) {
      o.okres = "Adwent"; o.kolor = FIOLET;
      const n = dz(s - adw, 7) + 1, rz = rzymska(n);
      if (dow === 0) { o.nazwa = `${rz} Niedziela Adwentu${n === 3 ? " (Gaudete)" : ""}`; o.ranga = R_PRZYWILEJ; if (n === 3) o.kolor = ROZOWY; }
      else if (m === 12 && d === 24) { o.nazwa = "Wigilia Bożego Narodzenia"; o.ranga = R_UPRZYW; }
      else { o.nazwa = `${dt} ${rz} tygodnia Adwentu`; o.ranga = m === 12 && d >= 17 ? R_UPRZYW : R_POWSZ; }
    } else if (D >= bn || D <= chrzest) {
      o.okres = "Okres Bożego Narodzenia"; o.kolor = BIALY;
      const rr = m === 12 ? r : r - 1, bnr = dniOdEpoki(rr, 12, 25), wbn = dzienTygodnia(bnr);
      const rodzina = wbn === 0 ? bnr + 5 : bnr + (7 - wbn);
      if (D === bn) { o.nazwa = "Uroczystość Narodzenia Pańskiego"; o.ranga = R_PRZYWILEJ; }
      else if (D === rodzina) { o.nazwa = "Święto Świętej Rodziny Jezusa, Maryi i Józefa"; o.ranga = R_NIEDZ; }
      else if (m === 12) { o.nazwa = `${dt} w oktawie Narodzenia Pańskiego`; o.ranga = R_UPRZYW; }
      else if (D === chrzest) { o.nazwa = "Święto Chrztu Pańskiego"; o.ranga = R_NIEDZ; }
      else if (dow === 0) { o.nazwa = "II Niedziela po Narodzeniu Pańskim"; o.ranga = R_NIEDZ; }
      else o.nazwa = d < 6 ? `${dt} okresu Bożego Narodzenia` : `${dt} po Objawieniu Pańskim`;
    } else if (D < popiel) {
      o.okres = "Okres zwykły";
      const n = s === chrzest ? 1 : dz(s - chrzest, 7) + 1, rz = rzymska(n);
      if (dow === 0) { o.nazwa = `${rz} Niedziela zwykła`; o.ranga = R_NIEDZ; } else o.nazwa = `${dt} ${rz} tygodnia zwykłego`;
    } else if (D < palm) {
      o.okres = "Wielki Post"; o.kolor = FIOLET; o.ranga = R_UPRZYW;
      if (D === popiel) { o.nazwa = "Środa Popielcowa"; o.ranga = R_PRZYWILEJ; }
      else if (D < popiel + 4) o.nazwa = `${dt} po Popielcu`;
      else {
        const n = dz(s - (popiel + 4), 7) + 1, rz = rzymska(n);
        if (dow === 0) { o.nazwa = `${rz} Niedziela Wielkiego Postu${n === 4 ? " (Laetare)" : ""}`; o.ranga = R_PRZYWILEJ; if (n === 4) o.kolor = ROZOWY; }
        else o.nazwa = `${dt} ${rz} tygodnia Wielkiego Postu`;
      }
    } else if (D < wczw) {
      o.okres = "Wielki Tydzień"; o.ranga = R_PRZYWILEJ;
      if (D === palm) { o.nazwa = "Niedziela Palmowa Męki Pańskiej"; o.kolor = CZERWONY; }
      else { o.nazwa = ["", "Wielki Poniedziałek", "Wielki Wtorek", "Wielka Środa"][dow]; o.kolor = FIOLET; }
    } else if (D < E) {
      o.okres = "Triduum Paschalne"; o.ranga = R_PRZYWILEJ;
      if (D === wczw) { o.nazwa = "Wielki Czwartek - Msza Wieczerzy Pańskiej"; o.kolor = BIALY; }
      else if (D === E - 2) { o.nazwa = "Wielki Piątek Męki Pańskiej"; o.kolor = CZERWONY; }
      else { o.nazwa = "Wielka Sobota"; o.uwaga = "wieczorem Wigilia Paschalna - kolor biały"; o.kolor = FIOLET; }
    } else if (D <= piecdz) {
      o.okres = "Okres wielkanocny"; o.kolor = BIALY;
      const n = dz(s - E, 7) + 1, rz = rzymska(n);
      if (D === E) { o.nazwa = "Niedziela Zmartwychwstania Pańskiego"; o.ranga = R_PRZYWILEJ; }
      else if (D < E + 7) { o.nazwa = `${dt} w oktawie Wielkanocy`; o.ranga = R_PRZYWILEJ; }
      else if (D === E + 7) { o.nazwa = "II Niedziela Wielkanocna (Miłosierdzia Bożego)"; o.ranga = R_PRZYWILEJ; }
      else if (D === E + 42) { o.nazwa = "Uroczystość Wniebowstąpienia Pańskiego"; o.ranga = R_PRZYWILEJ; }
      else if (D === piecdz) { o.nazwa = "Uroczystość Zesłania Ducha Świętego"; o.kolor = CZERWONY; o.ranga = R_PRZYWILEJ; }
      else if (dow === 0) { o.nazwa = `${rz} Niedziela Wielkanocna`; o.ranga = R_PRZYWILEJ; }
      else o.nazwa = `${dt} ${rz} tygodnia wielkanocnego`;
    } else {
      o.okres = "Okres zwykły";
      const n = 34 - dz(krol - s, 7), rz = rzymska(n);
      if (dow === 0) { o.nazwa = `${rz} Niedziela zwykła`; o.ranga = R_NIEDZ; } else o.nazwa = `${dt} ${rz} tygodnia zwykłego`;
      if (D === piecdz + 1) ustaw(o, "Wspomnienie NMP Matki Kościoła", BIALY, R_WSPOM);
      else if (D === E + 53) ustaw(o, "Święto Jezusa Chrystusa, Najwyższego i Wiecznego Kapłana", BIALY, R_SWIETO);
      else if (D === piecdz + 7) ustaw(o, "Uroczystość Najświętszej Trójcy", BIALY, R_UROCZ);
      else if (D === E + 60) ustaw(o, "Uroczystość Najświętszego Ciała i Krwi Chrystusa", BIALY, R_UROCZ);
      else if (D === E + 68) ustaw(o, "Uroczystość Najświętszego Serca Pana Jezusa", BIALY, R_UROCZ);
      else if (D === E + 69) ustaw(o, "Wspomnienie Niepokalanego Serca NMP", BIALY, R_WSPOM);
      else if (D === krol) ustaw(o, "Uroczystość Jezusa Chrystusa, Króla Wszechświata", BIALY, R_UROCZ);
    }
    return o;
  }

  function terminPrzeniesienia(r, f) {
    const Ds = dniOdEpoki(r, f.m, f.d), E = wielkanoc(r);
    if (f.m === 3 && f.d === 19 && Ds >= E - 7 && Ds < E) return E - 8;
    if (f.m === 6 && f.d === 24) return Ds - 1;
    let t = Ds;
    for (let i = 0; i < 20; i++) {
      const x = dataZDni(t), b = sezon(x.r, x.m, x.d), ob = szukajObchodu(x.m, x.d);
      if (b.ranga > R_UROCZ && !(t !== Ds && ob && ob.ranga === R_UROCZ)) break;
      t++;
    }
    return t;
  }

  /* co danego dnia obchodzi Kościół */
  function liturgia(r, m, d) {
    const o = sezon(r, m, d), D = dniOdEpoki(r, m, d), f = szukajObchodu(m, d);
    if (f) {
      if (f.ranga < o.ranga || (f.panskie && o.ranga === R_NIEDZ)) ustaw(o, f.nazwa, f.kolor, f.ranga);
      else if (f.ranga === R_UROCZ && o.ranga <= R_UROCZ) {
        const t = dataZDni(terminPrzeniesienia(r, f));
        o.uwaga = `${f.nazwa} - przeniesiona na ${t.d} ${MIES_DOP[t.m]}`;
      } else if (f.ranga === R_WSPOM && o.ranga === R_UPRZYW) o.uwaga = "lub dowolnie: " + f.nazwa;
    }
    if (o.ranga > R_UROCZ) {
      for (const u of OBCHODY) {
        if (u.ranga !== R_UROCZ) continue;
        const Ds = dniOdEpoki(r, u.m, u.d);
        if (Ds === D || Ds < D - 20 || Ds > D + 1) continue;
        if (sezon(r, u.m, u.d).ranga > R_UROCZ) continue;
        if (terminPrzeniesienia(r, u) === D) { ustaw(o, u.nazwa, u.kolor, R_UROCZ); o.uwaga = `przeniesiona z ${u.d} ${MIES_DOP[u.m]}`; break; }
      }
    }
    o.kolorNazwa = KOLORY[o.kolor];
    o.dzienTygodnia = dzienTygodnia(D);
    return o;
  }

  return { liturgia, dniOdEpoki, dataZDni, dzienTygodnia, dniWMiesiacu, wielkanoc, adwent1,
           DNI, DNI_W, MIES_DOP, MIES, KOLORY, ZIELONY, FIOLET, BIALY, CZERWONY, ROZOWY, R_UROCZ, R_SWIETO, R_NIEDZ, R_PRZYWILEJ };
})();
if (typeof module !== "undefined") module.exports = LIT;
