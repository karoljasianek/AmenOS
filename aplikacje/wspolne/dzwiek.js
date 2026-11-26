/* AmenOS - dźwięk w programach (WebAudio): organy kościelne, dzwon, dzwonki ministranckie,
 * pogłos jak w kościele i drobne dźwięki programów. Przeniesione z syntezatora jądra AmenOS. */
"use strict";

const Dzwiek = (() => {
  let ctx = null, wyj, poglosWej, falaOrgan = null;

  function start() {
    if (ctx) { if (ctx.state === "suspended") ctx.resume(); return ctx; }
    ctx = new (window.AudioContext || window.webkitAudioContext)();
    wyj = ctx.createGain(); wyj.gain.value = 0.8;
    const ogr = ctx.createDynamicsCompressor();
    ogr.threshold.value = -10; ogr.ratio.value = 6; ogr.attack.value = 0.003; ogr.release.value = 0.25;
    wyj.connect(ogr); ogr.connect(ctx.destination);
    const splot = ctx.createConvolver();
    splot.buffer = impuls(2.6, 2.4);
    poglosWej = ctx.createGain(); poglosWej.gain.value = 0.35;
    poglosWej.connect(splot); splot.connect(wyj);
    return ctx;
  }
  /* odpowiedź impulsowa "kościoła": szum zanikający wykładniczo, coraz ciemniejszy */
  function impuls(sek, rozpad) {
    const sr = ctx.sampleRate, n = Math.floor(sr * sek), b = ctx.createBuffer(2, n, sr);
    for (let c = 0; c < 2; c++) {
      const d = b.getChannelData(c);
      let lp = 0;
      for (let i = 0; i < n; i++) {
        const t = i / n, a = 0.9 - 0.75 * t;
        lp = lp * a + (Math.random() * 2 - 1) * (1 - a);
        d[i] = i < sr * 0.018 ? 0 : lp * Math.pow(1 - t, rozpad) * 3;
      }
    }
    return b;
  }
  function tor(glos, poglos) {
    const g = ctx.createGain(); g.gain.value = glos; g.connect(wyj);
    if (poglos) { const s = ctx.createGain(); s.gain.value = poglos; g.connect(s); s.connect(poglosWej); }
    return g;
  }
  const midi = n => 440 * Math.pow(2, (n - 69) / 12);

  /* organy: rejestry względem tonu 16' (1 = 16', 2 = 8', 4 = 4', 6 = 2 2/3', 8 = 2') */
  function fala() {
    if (falaOrgan) return falaOrgan;
    const h = { 1: .45, 2: 1, 3: .08, 4: .62, 6: .30, 8: .36, 10: .06, 12: .14, 16: .10 };
    const re = new Float32Array(17), im = new Float32Array(17);
    for (const k in h) im[k] = h[k];
    return (falaOrgan = ctx.createPeriodicWave(re, im));
  }
  function organy(hz, t, dl, glos = 0.16) {
    start();
    const wy = tor(1, 0.7), g = ctx.createGain(), gc = ctx.createGain();
    g.connect(wy); gc.connect(wy);
    const o = ctx.createOscillator(), c = ctx.createOscillator();
    o.setPeriodicWave(fala()); c.setPeriodicWave(fala());
    o.frequency.value = hz / 2; c.frequency.value = hz / 2 * 1.0035;           /* voix céleste */
    o.connect(g); c.connect(gc);
    for (const [x, v, at] of [[g, glos, 0.03], [gc, glos * 0.33, 0.07]]) {
      x.gain.setValueAtTime(0, t); x.gain.linearRampToValueAtTime(v, t + at);
      x.gain.setValueAtTime(v, t + dl); x.gain.setTargetAtTime(0, t + dl, 0.05);
    }
    o.start(t); c.start(t); o.stop(t + dl + 0.6); c.stop(t + dl + 0.6);
    return { zatrzymaj: k => { const w = Math.max(k, t); g.gain.cancelScheduledValues(w); gc.gain.cancelScheduledValues(w);
             g.gain.setTargetAtTime(0, w, 0.04); gc.gain.setTargetAtTime(0, w, 0.04); try { o.stop(w + 0.4); c.stop(w + 0.4); } catch (e) {} } };
  }
  function sinusy(czesci, t, glos, poglos, pan = 0) {
    start();
    const wy = tor(1, poglos);
    let p = wy;
    if (ctx.createStereoPanner) { p = ctx.createStereoPanner(); p.pan.value = pan; p.connect(wy); }
    for (const [hz, a, tau] of czesci) {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.frequency.value = hz; o.connect(g); g.connect(p);
      g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(a * glos, t + 0.004);
      g.gain.setTargetAtTime(0, t + 0.004, tau);
      o.start(t); o.stop(t + tau * 7);
    }
  }
  /* dzwon kościelny: hum, prima, tercja mała, kwinta, nominał... - każdy alikwot wybrzmiewa po swojemu */
  function dzwon(hz = 196, t, glos = 0.5) {
    start(); t = t ?? ctx.currentTime + 0.02;
    const r = [.5, 1, 1.189, 1.498, 2, 2.52, 2.99, 4.07, 5.01], a = [.15, .12, .11, .06, .17, .07, .05, .028, .018], tau = [4.5, 3.2, 2.4, 1.6, 1.8, 1, .65, .38, .24];
    sinusy(r.map((x, i) => [hz * x, a[i] * 2.4, tau[i] / 2.3]), t, glos, 0.9);
  }
  function dzwonek(hz, t, glos) {
    const r = [1, 2.756, 5.404, 8.933], a = [1, .47, .23, .12], tau = [.42, .19, .08, .04];
    sinusy(r.map((x, i) => [hz * x, a[i], tau[i] / 2.3]), t, glos, 0.5, Math.random() * 1.2 - 0.6);
  }
  /* dzwonki ministranckie: kilka małych dzwonków potrząsanych naraz; zwraca czas w ms */
  function dzwonki(serie = 3) {
    start();
    const f = [2093, 2349, 2637, 3136], t0 = ctx.currentTime + 0.05;
    for (let s = 0; s < serie; s++)
      for (let k = 0; k < 0.45; k += 0.038 + Math.random() * 0.014) dzwonek(f[Math.floor(Math.random() * 4)], t0 + s * 0.67 + k, 0.10 + Math.random() * 0.06);
    return serie * 670 + 300;
  }
  function bij(n, hz = 196, odstep = 1.9) { start(); const t0 = ctx.currentTime + 0.05; for (let i = 0; i < n; i++) dzwon(hz, t0 + i * odstep, 0.5); }
  function pik(hz = 784, t, glos = 0.25) { start(); t = t ?? ctx.currentTime + 0.01; sinusy([[hz, 1, .11], [hz * 2, .33, .05], [hz * 3, .12, .025]], t, glos, 0.35); }

  const efekty = {
    powiadom() { pik(784); pik(1046.5, ctx.currentTime + 0.11); },
    wiadomosc() { pik(1318.5, undefined, 0.18); pik(1568, ctx.currentTime + 0.07, 0.15); },
    klik() { start(); sinusy([[1760, 1, .008]], ctx.currentTime + 0.005, 0.25, 0.05); },
    paciorek() { start(); sinusy([[1174.7, 1, .012], [2349.3, .25, .05]], ctx.currentTime + 0.005, 0.3, 0.3); },
    amen() {                                       /* kadencja plagalna: F-dur, potem C-dur */
      start(); const t = ctx.currentTime + 0.05;
      for (const n of [53, 65, 69, 72]) organy(midi(n), t, 0.75, 0.11);
      for (const n of [48, 64, 67, 72]) organy(midi(n), t + 0.8, 1.6, 0.11);
    },
    akord() { start(); const t = ctx.currentTime + 0.03; for (let i = 0; i < 4; i++) organy(midi(67 + Math.floor(Math.random() * 12)), t + i * 0.09, 0.9, 0.08); },
    start() { dzwon(196); const t = ctx.currentTime + 0.5; for (const n of [48, 60, 64, 67]) organy(midi(n), t, 2.6, 0.08); },
    niszczarka(sek = 1.7) {                        /* "wrrrr": szum przez filtr, modulowany */
      start();
      const n = Math.floor(ctx.sampleRate * sek), b = ctx.createBuffer(1, n, ctx.sampleRate), d = b.getChannelData(0);
      for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * (0.6 + 0.4 * Math.sin(i / ctx.sampleRate * 2 * Math.PI * 31)) * Math.min(1, i / 2000, (n - i) / 4000);
      const s = ctx.createBufferSource(), f = ctx.createBiquadFilter();
      s.buffer = b; f.type = "bandpass"; f.frequency.value = 900; f.Q.value = 0.7;
      s.connect(f); f.connect(tor(0.5, 0.1)); s.start();
    },
  };
  function efekt(nazwa, ...a) { try { start(); efekty[nazwa](...a); } catch (e) { /* bez dźwięku */ } }

  /* utwór z danych AmenOS: nuty MIDI (0 = pauza) i czasy w ms */
  function utwor(u, naKoniec) {
    start();
    const t0 = ctx.currentTime + 0.15, glosy = [];
    let t = t0;
    u.nuty.forEach((n, i) => { const dl = u.czasy[i] / 1000; if (n) glosy.push(organy(midi(n), t, dl * 0.93, 0.15)); t += dl; });
    const calosc = t - t0;
    const tm = setTimeout(() => naKoniec && naKoniec(), (calosc + 0.4) * 1000);
    return {
      calosc: calosc * 1000,
      postep: () => Math.max(0, Math.min(calosc, ctx.currentTime - t0)) * 1000,
      zatrzymaj() { clearTimeout(tm); const k = ctx.currentTime; glosy.forEach(g => g.zatrzymaj(k)); },
    };
  }

  return { start, organy, dzwon, dzwonki, bij, pik, efekt, utwor, midi };
})();
