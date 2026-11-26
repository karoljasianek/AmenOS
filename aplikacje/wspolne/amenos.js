/* AmenOS - wspólne funkcje programów parafialnych: dane parafii (zapisywane na komputerze
 * i widoczne we wszystkich oknach), kolor liturgiczny dnia, daty, Msze, komunikaty. */
"use strict";

const Amen = (() => {
  const P = "amenos:";
  const KOLOR_LIT = ["#2F9E5B", "#7D4CC9", "#F3EEE0", "#D23B3B", "#E07BA8"];
  const AKCENT_LIT = ["#23874B", "#7046C2", "#B8901C", "#C8343A", "#D0628F"];

  function wczytaj(k, dom) {
    try { const v = localStorage.getItem(P + k); return v === null ? JSON.parse(JSON.stringify(dom)) : JSON.parse(v); }
    catch (e) { return dom; }
  }
  function zapisz(k, v) { try { localStorage.setItem(P + k, JSON.stringify(v)); } catch (e) { /* brak miejsca */ } }
  /* zmiana danych w innym oknie (np. pieśni ustawione na tablicy widać od razu na rzutniku) */
  function naZmiane(k, fn) { addEventListener("storage", e => { if (e.key === P + k) fn(wczytaj(k)); }); }

  function teraz() {
    const t = new Date();
    return { r: t.getFullYear(), m: t.getMonth() + 1, d: t.getDate(), h: t.getHours(), min: t.getMinutes(), s: t.getSeconds(), dow: t.getDay() };
  }
  function liturgiaDzis() { const t = teraz(); return LIT.liturgia(t.r, t.m, t.d); }
  /* mieszanie kolorów (bez CSS color-mix, żeby działało też w starszych silnikach) */
  function miesz(a, b, t) {
    const x = h => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16)), p = x(a), q = x(b);
    return "#" + p.map((v, i) => Math.round(v + (q[i] - v) * t).toString(16).padStart(2, "0")).join("");
  }
  function ustawAkcent() {
    const l = liturgiaDzis(), s = document.documentElement.style, a = AKCENT_LIT[l.kolor];
    s.setProperty("--akcent", a);
    s.setProperty("--kolor-lit", KOLOR_LIT[l.kolor]);
    s.setProperty("--akcent-tlo", miesz(a, "#ffffff", 0.9));
    s.setProperty("--akcent-jasny", miesz(a, "#ffffff", 0.8));
    s.setProperty("--akcent-mgla", miesz(a, "#ffffff", 0.75));
    s.setProperty("--akcent-ciemny", miesz(a, "#000000", 0.14));
    s.setProperty("--akcent-noc", miesz(a, "#1a1446", 0.5));
    s.setProperty("--akcent-tekst", miesz(a, "#000000", 0.3));
    return l;
  }
  function dataSlownie(r, m, d) { return `${LIT.DNI_W[LIT.dzienTygodnia(LIT.dniOdEpoki(r, m, d))]}, ${d} ${LIT.MIES_DOP[m]} ${r}`; }

  /* dane parafii */
  const parafia = () => wczytaj("parafia", "Parafia Rzymskokatolicka");
  const piesni = () => wczytaj("piesni", ["", "", "", "", "", ""]);
  const msze = () => wczytaj("msze", AMEN_DANE.msze);
  const ogloszenia = () => wczytaj("ogloszenia", AMEN_DANE.ogloszenia);

  /* godziny w dowolnym tekście: "7:00, 9:00 (suma), 18.30" -> minuty od północy */
  function godziny(tekst) {
    const w = [];
    for (const m of String(tekst).matchAll(/(\d{1,2})[:.](\d{2})/g)) { const h = +m[1]; if (h < 24) w.push(h * 60 + +m[2]); }
    return w.sort((a, b) => a - b);
  }
  function wierszMszyDnia(r, m, d) {
    const l = LIT.liturgia(r, m, d), dow = LIT.dzienTygodnia(LIT.dniOdEpoki(r, m, d));
    if (dow === 0 || l.ranga <= LIT.R_UROCZ) return 0;
    return dow === 6 ? 2 : 1;
  }
  /* najbliższa Msza: { kiedy: 0 dziś / 1 jutro, godz (min), za (min), trwa } albo null */
  function najblizszaMsza() {
    const t = teraz(), ms = msze(), now = t.h * 60 + t.min;
    for (const g of godziny(ms[wierszMszyDnia(t.r, t.m, t.d)].godziny)) {
      if (g <= now && now - g < 45) return { kiedy: 0, godz: g, za: 0, trwa: true };
      if (g > now) return { kiedy: 0, godz: g, za: g - now, trwa: false };
    }
    const j = LIT.dataZDni(LIT.dniOdEpoki(t.r, t.m, t.d) + 1), g = godziny(ms[wierszMszyDnia(j.r, j.m, j.d)].godziny);
    return g.length ? { kiedy: 1, godz: g[0], za: 24 * 60 - now + g[0], trwa: false } : null;
  }
  function komentarzMszy(n) {
    if (!n) return "Wpisz godziny Mszy w programie „Porządek Mszy” - wtedy policzę, ile zostało czasu.";
    if (n.trwa) return "Msza właśnie trwa! Wchodź po cichu, najlepiej bocznymi drzwiami.";
    if (n.kiedy) return "Na dziś to już wszystko. Jutro rano - nie zaśpij!";
    if (n.za <= 10) return "Biegiem! Ministranci już zapalają świece.";
    if (n.za <= 30) return "Czas wkładać buty. Miejsca w ostatniej ławce już zajęte.";
    if (n.za <= 120) return "Zdążysz spokojnie - może nawet usiądziesz z przodu.";
    return "Mnóstwo czasu. Zdążysz jeszcze ugotować rosół.";
  }
  const hm = g => `${Math.floor(g / 60)}:${String(g % 60).padStart(2, "0")}`;
  const zaIle = z => z >= 60 ? `za ${Math.floor(z / 60)} h ${z % 60} min` : `za ${z} min`;

  /* otwiera inny program AmenOS w nowym oknie */
  function otworz(program) { window.open(`../${program}/index.html`, "_blank"); }

  let dymekT;
  function komunikat(tekst, ms = 2600) {
    let d = document.querySelector(".dymek-info");
    if (!d) { d = document.createElement("div"); d.className = "dymek-info"; document.body.appendChild(d); }
    d.textContent = tekst;
    requestAnimationFrame(() => d.classList.add("widac"));
    clearTimeout(dymekT);
    dymekT = setTimeout(() => d.classList.remove("widac"), ms);
  }

  const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

  return { miesz, wczytaj, zapisz, naZmiane, teraz, liturgiaDzis, ustawAkcent, dataSlownie, parafia, piesni, msze, ogloszenia,
           godziny, najblizszaMsza, komentarzMszy, hm, zaIle, otworz, komunikat, esc, KOLOR_LIT, AKCENT_LIT };
})();
