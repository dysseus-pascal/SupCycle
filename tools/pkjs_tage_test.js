// Tage und Anker auf der Telefonseite (src/pkjs/index.js).
//
//   node tools/pkjs_tage_test.js          (die Zeitzone kommt aus TZ)
//
// Was hier leicht falsch und teuer ist:
//
//   - DER TAG IST EIN DATUM (Audit M1), wie auf der Uhr: ein Plan, der
//     "beginnt heute" sagt, traegt als Anker den heutigen Kalendertag - auch
//     oestlich von Greenwich und an den Umstellungstagen.
//   - DIE UMSTELLUNG DES TELEFONSPEICHERS: ein gespeicherter, noch nicht
//     zugestellter Plan von 0.15.0 traegt Anker der alten Zaehlung. Geht er so
//     an die neue Uhr, steht der Zyklus einen Tag daneben.
//   - DER ANKER UEBERSTEHT DIE KONFIGSEITE (Audit W-H1). Bis 0.15.0 rechnete
//     jedes Speichern ihn aus "laeuft seit N Wochen" neu und verschob den
//     Zyklus um bis zu sechs Tage, das Raster "alle X Tage" gleich mit.
//
// Die Plaene sind von Hand nach src/c/plan.h gebaut, die Erwartungen ueber
// Date.parse eines ISO-Datums gerechnet - nicht mit index.js selbst.
//
// Exitcode 0 = alles wie zugesagt.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const Module = require('module');

const SRC = process.env.SC_SRC || path.join(__dirname, '..', 'src', 'pkjs', 'index.js');
const CFG = path.join(__dirname, '..', 'src', 'pkjs', 'config.js');

let fails = 0;
function check(name, ok, detail) {
  console.log((ok ? '  ok     ' : '  FEHLER ') + name + (ok ? '' : '   -> ' + detail));
  if (!ok) fails++;
}

// Ein Date, dessen "jetzt" der Test bestimmt - Datum und Uhrzeit in der
// Ortszeit des Prozesses (TZ).
function festeZeit(ms) {
  return class FesteZeit extends Date {
    constructor(...a) { if (a.length === 0) super(ms); else super(...a); }
    static now() { return ms; }
  };
}

// Eine Welt wie in pkjs_start_test.js: index.js frisch geladen,
// Telefonspeicher vorbelegbar, Nachrichten an die Uhr abgefangen.
function world(store, jetztMs) {
  store = store || {};
  const sent = [];
  const logs = [];
  let url = null;
  function FakeClay() {
    this.generateUrl = () => 'about:blank';
    this.getSettings = (response) => {
      const raw = JSON.parse(response);
      const out = {};
      Object.keys(raw).forEach((k) => { out[k] = { value: raw[k] }; });
      return out;
    };
  }
  const sandbox = {
    console: { log: (m) => logs.push(String(m)) },
    Date: jetztMs === undefined ? Date : festeZeit(jetztMs),
    Math, JSON, parseInt, parseFloat, isNaN, isFinite,
    String, Number, Object, Array, setTimeout, clearTimeout,
    XMLHttpRequest: function () {},
    localStorage: {
      getItem: (k) => (k in store ? store[k] : null),
      setItem: (k, v) => { store[k] = String(v); },
      removeItem: (k) => { delete store[k]; },
    },
    Pebble: {
      addEventListener: (ev, fn) => { (sandbox.__ev[ev] = sandbox.__ev[ev] || []).push(fn); },
      sendAppMessage: (msg, ok, nok) => { sent.push({ msg: msg, ok: ok, nok: nok }); },
      openURL: (u) => { url = u; },
    },
    __ev: {},
  };
  sandbox.module = { exports: {} };
  sandbox.require = function (id) {
    if (id === '@rebble/clay') return FakeClay;
    if (id === './config') return require(CFG);
    return Module.createRequire(SRC)(id);
  };
  vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync(SRC, 'utf8'), sandbox, { filename: SRC });
  return {
    store, sent, logs, url: () => url,
    fire: (ev, arg) => (sandbox.__ev[ev] || []).forEach((fn) => fn(arg)),
    clay: () => JSON.parse(store['clay-settings'] || '{}'),
  };
}

// --- Plaene von Hand, nach src/c/plan.h ---
const SLOT = 26;
function slot(name, every, on, off, anker) {
  const b = new Array(SLOT).fill(0);
  Buffer.from(name, 'utf8').forEach((x, i) => { if (i < 15) b[i] = x; });
  b[16] = 8; b[17] = 0; b[18] = 1; b[19] = every; b[20] = on; b[21] = off;
  for (let i = 0; i < 4; i++) b[22 + i] = (anker >> (8 * i)) & 0xff;
  return b;
}
function plan(...plaetze) {
  const out = [];
  for (let i = 0; i < 6; i++) out.push(...(plaetze[i] || new Array(SLOT).fill(0)));
  return out;
}
function anker(bytes, i) {
  const o = i * SLOT;
  return bytes[o + 22] | (bytes[o + 23] << 8) | (bytes[o + 24] << 16) | (bytes[o + 25] << 24);
}
// Der Kalendertag eines Datums: ISO-Datum ohne Uhrzeit liest JavaScript als UTC.
function kalendertag(y, m, d) {
  const iso = y + '-' + String(m).padStart(2, '0') + '-' + String(d).padStart(2, '0');
  return Date.parse(iso) / 86400000;
}
// Ortszeit dieses Prozesses als ms seit 1970.
function ortszeit(y, m, d, h, mi) {
  return new Date(y, m - 1, d, h, mi || 0).getTime();
}
// So zaehlte 0.15.0 auf dem Telefon: Ortsmitternacht durch einen Tag.
function tagBis015(ms) {
  const n = new Date(ms);
  return Math.floor(new Date(n.getFullYear(), n.getMonth(), n.getDate()).getTime() / 86400000);
}

console.log('\nZeitzone: ' + (process.env.TZ || '(Rechner)'));

console.log('\nDer Anker ist der Kalendertag (M1)');
[[2026, 1, 14, 9], [2026, 3, 29, 12], [2026, 10, 25, 0], [2026, 10, 25, 12], [2026, 10, 25, 23],
 [2026, 11, 1, 12], [2027, 3, 28, 12]].forEach(([y, m, d, h]) => {
  const w = world({}, ortszeit(y, m, d, h, 30));
  w.fire('webviewclosed', { response: JSON.stringify({ COUNT: '1', NAME1: 'Zink', TIME1: '480', SINCE1: '0' }) });
  const b = w.sent[0] ? w.sent[0].msg.PLAN : [];
  const soll = kalendertag(y, m, d);
  check(y + '-' + m + '-' + d + ' ' + h + ':30: "beginnt heute" ist der Kalendertag', anker(b, 0) === soll,
        anker(b, 0) + ' statt ' + soll);
});

console.log('\nUmstellung des Telefonspeichers (M1)');
[[2026, 1, 14, 9], [2026, 7, 14, 9], [2026, 7, 14, 23]].forEach(([y, m, d, h]) => {
  const jetzt = ortszeit(y, m, d, h, 50);
  const alt = tagBis015(jetzt);
  const versatz = kalendertag(y, m, d) - alt;
  const gespeichert = plan(slot('Zink', 2, 0, 0, alt), slot('Maca', 1, 1, 1, alt - 7));
  const items = [{ name: 'Zink', hour: 8, minute: 0, every: 2, on: 0, off: 0, anchor: alt },
                 { name: 'Maca', hour: 8, minute: 0, every: 1, on: 1, off: 1, anchor: alt - 7 }, null, null, null, null];
  const wann = y + '-' + m + '-' + d + ' ' + h + ':50';
  // Auf der Konfigseite gespeichert, nie angekommen: beim Start geht er an die Uhr.
  const w = world({ supcycle_plan: JSON.stringify(gespeichert), supcycle_items: JSON.stringify(items),
                    supcycle_pending: '1', supcycle_fx: '1' }, jetzt);
  w.fire('appmessage', { payload: { PLAN: plan(), REQUEST: 1, LANG: 1, TODAY: 20260101, DUE: 0, TAKEN: 0 } });
  const b = w.sent[0] ? w.sent[0].msg.PLAN : [];
  check(wann + ': nachgereichter Plan traegt Kalendertage', anker(b, 0) === alt + versatz && anker(b, 1) === alt - 7 + versatz,
        anker(b, 0) + '/' + anker(b, 1) + ' statt ' + (alt + versatz) + '/' + (alt - 7 + versatz));
  const it = JSON.parse(w.store.supcycle_items);
  check(wann + ': gespeicherte Eintraege ebenso', it[0].anchor === alt + versatz && it[1].anchor === alt - 7 + versatz,
        it[0].anchor + '/' + it[1].anchor);
  check(wann + ': umgestellt ist vermerkt', w.store.supcycle_tage === '2', w.store.supcycle_tage);
  // Ein zweites Ereignis verschiebt nichts mehr.
  w.fire('appmessage', { payload: { PLAN: plan(), REQUEST: 1, LANG: 1, TODAY: 20260101, DUE: 0, TAKEN: 0 } });
  const b2 = w.sent[1] ? w.sent[1].msg.PLAN : [];
  check(wann + ': zweites Mal unveraendert', anker(b2, 0) === alt + versatz, anker(b2, 0));
});
{
  // Ein frisches Telefon: nichts zu verschieben, aber vermerkt.
  const w = world({});
  w.fire('ready');
  check('frisches Telefon: vermerkt, ohne Plan', w.store.supcycle_tage === '2' && !w.store.supcycle_plan,
        JSON.stringify(w.store));
}

console.log('\nDer Anker uebersteht die Konfigseite (W-H1)');
{
  // Die Uhr meldet: Maca 8 an/2 aus seit 10 Tagen, Zink alle 2 Tage ab
  // gestern. Die Seite zeigt "seit 1 Woche" und "seit 0 Wochen".
  const jetzt = ortszeit(2026, 7, 14, 9);
  const heute = kalendertag(2026, 7, 14);
  const uhr = plan(slot('Maca', 1, 8, 2, heute - 10), slot('Zink', 2, 0, 0, heute - 1));
  const w = world({}, jetzt);
  w.fire('appmessage', { payload: { PLAN: uhr, FX: 1, TODAY: 20260714, DUE: 1, TAKEN: 0 } });
  w.fire('showConfiguration');
  const c = w.clay();
  check('die Seite zeigt seit 1 und 0 Wochen', c.SINCE1 === '1' && c.SINCE2 === '0', c.SINCE1 + '/' + c.SINCE2);
  // Unveraendert gespeichert: genau das kommt aus der Seite zurueck.
  w.fire('webviewclosed', { response: JSON.stringify(c) });
  const b = w.sent[0] ? w.sent[0].msg.PLAN : [];
  check('unveraendert gespeichert: Maca behaelt seinen Anker (nicht 3 Tage spaeter)', anker(b, 0) === heute - 10,
        (anker(b, 0) - heute) + ' statt -10');
  check('unveraendert gespeichert: das Raster alle 2 Tage bleibt (nicht heute faellig)', anker(b, 1) === heute - 1,
        (anker(b, 1) - heute) + ' statt -1');
  // Noch einmal oeffnen und speichern: weiter derselbe Anker.
  w.fire('showConfiguration');
  w.fire('webviewclosed', { response: JSON.stringify(w.clay()) });
  const b2 = w.sent[1] ? w.sent[1].msg.PLAN : [];
  check('zweimal gespeichert: immer noch derselbe', anker(b2, 0) === heute - 10 && anker(b2, 1) === heute - 1,
        (anker(b2, 0) - heute) + '/' + (anker(b2, 1) - heute));
  // Die Wochenzahl geaendert: dann gilt sie.
  const neu = Object.assign({}, w.clay(), { SINCE1: '3' });
  w.fire('webviewclosed', { response: JSON.stringify(neu) });
  const b3 = w.sent[2] ? w.sent[2].msg.PLAN : [];
  check('seit 3 Wochen eingestellt: Anker vor 21 Tagen', anker(b3, 0) === heute - 21, anker(b3, 0) - heute);
  check('der andere Platz bleibt', anker(b3, 1) === heute - 1, anker(b3, 1) - heute);
  // Ein neuer Name mit derselben Wochenzahl ist ein neues Praeparat.
  w.fire('showConfiguration');
  const umbenannt = Object.assign({}, w.clay(), { NAME2: 'Eisen' });
  w.fire('webviewclosed', { response: JSON.stringify(umbenannt) });
  const b4 = w.sent[3] ? w.sent[3].msg.PLAN : [];
  check('neuer Name, seit 0 Wochen: beginnt heute', anker(b4, 1) === heute, anker(b4, 1) - heute);
}
{
  // Die Seite wird Tage nach dem letzten Stand der Uhr geoeffnet: sie zeigt
  // die Wochen von HEUTE, und unveraendert gespeichert bleibt der Anker.
  const heute = kalendertag(2026, 7, 14);
  const vorher = world({}, ortszeit(2026, 7, 14, 9));
  vorher.fire('appmessage', { payload: { PLAN: plan(slot('Maca', 1, 8, 2, heute - 10)), FX: 1, TODAY: 20260714, DUE: 1, TAKEN: 0 } });
  const w = world(vorher.store, ortszeit(2026, 7, 26, 9));   // zwoelf Tage spaeter
  w.fire('showConfiguration');
  check('zwoelf Tage spaeter zeigt die Seite seit 3 Wochen', w.clay().SINCE1 === '3', w.clay().SINCE1);
  w.fire('webviewclosed', { response: JSON.stringify(w.clay()) });
  const b = w.sent[0] ? w.sent[0].msg.PLAN : [];
  check('und speichert den alten Anker', anker(b, 0) === heute - 10, anker(b, 0) - heute);
}

console.log('\nFehler: ' + fails);
process.exit(fails ? 1 : 0);
