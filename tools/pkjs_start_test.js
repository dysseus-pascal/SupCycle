// Die Startzweige der Telefonseite: was sie tut, wenn die Uhr sich meldet.
//
//   node tools/pkjs_start_test.js
//
// Die Uhr meldet mit JEDER Nachricht ihren Plan; beim Start der App traegt
// die Nachricht zusaetzlich REQUEST. Die Telefonseite entscheidet dann, wer
// recht hat: ihr gespeicherter Plan oder der der Uhr. Hier ist leicht etwas
// falsch, und es kostet Daten:
//
//   - Eine FRISCHE UHR meldet einen leeren Plan. Er darf den gespeicherten
//     nie ueberschreiben - auch dann nicht, wenn die Startanfrage verloren ging
//     und die Uhr ihren Stand ohne REQUEST nachholt (bis 0.14.0 genau so
//     geschehen: der Plan auf dem Telefon war danach leer).
//   - Eine auf der Konfigseite gespeicherte, nie angekommene Aenderung geht
//     beim naechsten Start an die Uhr - und kein Stand der Uhr ueberschreibt
//     sie vorher.
//   - Sonst gilt die Uhr: ihr Plan kommt auf das Telefon.
//   - Namen gehen als gueltiges UTF-8 hinaus, gekuerzt nur an Zeichengrenzen
//     (Umlaute 2 Byte, Emoji 4 Byte) - und kommen unveraendert zurueck.
//
// Die Plaene hier sind VON HAND nach dem Format in src/c/plan.h gebaut (26
// Byte je Platz), nicht mit buildPlan aus index.js - sonst prueften sich die
// beiden Haelften gegenseitig mit demselben Fehler.
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

// Eine Welt: index.js frisch geladen, Telefonspeicher vorbelegbar, Nachrichten
// an die Uhr abgefangen - samt ihren Rueckrufen, damit sich Ankommen und
// Nichtankommen getrennt ausloesen lassen.
function world(store) {
  store = store || {};
  const sent = [];
  const logs = [];
  function XHR() { this.status = 200; }
  XHR.prototype.open = function () {};
  XHR.prototype.setRequestHeader = function () {};
  XHR.prototype.send = function () {};
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
    Date, Math, JSON, parseInt, parseFloat, isNaN, isFinite,
    String, Number, Object, Array, setTimeout, clearTimeout,
    XMLHttpRequest: XHR,
    localStorage: {
      getItem: (k) => (k in store ? store[k] : null),
      setItem: (k, v) => { store[k] = String(v); },
      removeItem: (k) => { delete store[k]; },
    },
    Pebble: {
      addEventListener: (ev, fn) => { (sandbox.__ev[ev] = sandbox.__ev[ev] || []).push(fn); },
      sendAppMessage: (msg, ok, nok) => { sent.push({ msg: msg, ok: ok, nok: nok }); },
      openURL: () => {},
      getTimelineToken: undefined,
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
    store, sent, logs,
    fire: (ev, arg) => (sandbox.__ev[ev] || []).forEach((fn) => fn(arg)),
    planOnPhone: () => (store.supcycle_plan ? JSON.parse(store.supcycle_plan) : null),
  };
}

// --- Plaene von Hand, nach src/c/plan.h ---
const SLOT = 26;          // SC_ITEM_BYTES
const PLAN_LEN = 6 * SLOT; // SC_MAX_ITEMS * SC_ITEM_BYTES = 156
function slot(name, h, m) {
  const b = new Array(SLOT).fill(0);
  Buffer.from(name, 'utf8').forEach((x, i) => { if (i < 15) b[i] = x; });
  b[16] = h; b[17] = m;
  b[18] = 1;              // benutzt
  b[19] = 1;              // alle 1 Tage
  b[22] = 0x10; b[23] = 0x51;  // Ankertag 20752 = 0x5110, little endian
  return b;
}
function leer() { return new Array(SLOT).fill(0); }
function plan(...plaetze) {
  const out = [];
  for (let i = 0; i < 6; i++) out.push(...(plaetze[i] || leer()));
  return out;
}
const LEER = plan();
const TELEFON = plan(slot('Kreatin', 8, 0), slot('Vitamin D3', 8, 0), slot('Black Maca', 12, 30));
const UHR = plan(slot('Zink', 21, 15));
function namen(bytes) {
  const out = [];
  for (let i = 0; i < 6; i++) {
    if (!bytes[i * SLOT + 18]) continue;
    out.push(Buffer.from(bytes.slice(i * SLOT, i * SLOT + 16)).toString('utf8').replace(/\0+$/, ''));
  }
  return out.join(',');
}
// Ein Telefon, auf dem der Plan schon steht und bestaetigt ist.
function telefonMitPlan() {
  return { supcycle_plan: JSON.stringify(TELEFON), supcycle_fx: '1' };
}
// Die Tagesmeldung der Uhr: Plan, Stand, auf Wunsch mit REQUEST und LANG.
function meldung(planBytes, mitAnfrage, extra) {
  const p = { PLAN: planBytes.slice(), FX: 1, TODAY: 20261003, DUE: 0, TAKEN: 0, NAMES: '' };
  if (mitAnfrage) { p.REQUEST = 1; p.LANG = 1; }
  return Object.assign(p, extra || {});
}

console.log('\nFormat');
check('ein Plan hat 156 Byte', LEER.length === PLAN_LEN && TELEFON.length === 156, LEER.length);

console.log('\nStartanfrage (REQUEST)');
{
  const w = world(telefonMitPlan());
  w.fire('appmessage', { payload: meldung(LEER, true) });
  check('frische Uhr: der gespeicherte Plan geht an die Uhr',
        w.sent.length === 1 && namen(w.sent[0].msg.PLAN) === 'Kreatin,Vitamin D3,Black Maca',
        JSON.stringify(w.sent.map((s) => Object.keys(s.msg))));
  check('frische Uhr: der gespeicherte Plan bleibt',
        namen(w.planOnPhone()) === 'Kreatin,Vitamin D3,Black Maca', namen(w.planOnPhone() || []));
  check('frische Uhr: die Animation reist mit', w.sent[0] && w.sent[0].msg.FX === 1,
        w.sent[0] && w.sent[0].msg.FX);
  if (w.sent[0]) w.sent[0].ok();
  check('frische Uhr: steht nach der Bestaetigung im Log', w.logs.join('|').indexOf('Uhr ohne Plan') >= 0,
        w.logs.join('|'));
}
{
  const w = world(telefonMitPlan());
  w.fire('appmessage', { payload: meldung(UHR, true) });
  check('Uhr mit Plan: nichts geht hinaus', w.sent.length === 0, w.sent.length);
  check('Uhr mit Plan: ihr Plan kommt aufs Telefon', namen(w.planOnPhone()) === 'Zink',
        namen(w.planOnPhone() || []));
  const clay = JSON.parse(w.store['clay-settings'] || '{}');
  check('Uhr mit Plan: die Konfigseite zeigt ihn', clay.NAME1 === 'Zink' && clay.TIME1 === String(21 * 60 + 15)
        && clay.NAME2 === '' && clay.COUNT === '1', JSON.stringify(clay));
}
{
  const s = telefonMitPlan();
  s.supcycle_pending = '1';
  const w = world(s);
  w.fire('appmessage', { payload: meldung(UHR, true) });
  check('offene Aenderung: sie geht an die Uhr, nicht umgekehrt',
        w.sent.length === 1 && namen(w.sent[0].msg.PLAN) === 'Kreatin,Vitamin D3,Black Maca',
        w.sent.length && namen(w.sent[0].msg.PLAN));
  check('offene Aenderung: der Plan der Uhr wird nicht uebernommen',
        namen(w.planOnPhone()) === 'Kreatin,Vitamin D3,Black Maca', namen(w.planOnPhone() || []));
  if (w.sent[0]) w.sent[0].ok();
  check('offene Aenderung: erst die Bestaetigung loescht den Vermerk', w.store.supcycle_pending === undefined,
        w.store.supcycle_pending);
}
{
  const s = telefonMitPlan();
  s.supcycle_pending = '1';
  const w = world(s);
  w.fire('appmessage', { payload: meldung(UHR, true) });
  if (w.sent[0]) w.sent[0].nok();
  check('offene Aenderung, Uhr lehnt ab: der Vermerk bleibt', w.store.supcycle_pending === '1',
        w.store.supcycle_pending);
}
{
  const w = world(telefonMitPlan());
  const p = meldung(LEER, true);
  delete p.PLAN;
  w.fire('appmessage', { payload: p });
  check('aeltere Uhr ohne Planmeldung: der gespeicherte Plan geht hin',
        w.sent.length === 1 && namen(w.sent[0].msg.PLAN) === 'Kreatin,Vitamin D3,Black Maca',
        w.sent.length);
}
{
  const w = world({});
  w.fire('appmessage', { payload: meldung(LEER, true) });
  check('weder hier noch dort ein Plan: nichts geht hinaus', w.sent.length === 0, w.sent.length);
}
{
  // Auf der Konfigseite geleert, bestaetigt: hier UND auf der Uhr leer. Das
  // ist kein Loch, sondern der Wille - nichts zurueckschicken.
  const w = world({ supcycle_plan: JSON.stringify(LEER) });
  w.fire('appmessage', { payload: meldung(LEER, true) });
  check('beide leer: nichts geht hinaus', w.sent.length === 0, w.sent.length);
}
{
  const w = world(telefonMitPlan());
  w.fire('appmessage', { payload: meldung(UHR, true) });
  check('die Sprache der Uhr wird gemerkt', w.store.supcycle_lang === '1', w.store.supcycle_lang);
}

console.log('\nOhne Anfrage (Tagesmeldung, Nachholen)');
{
  // DER FALL AUS H3: Startanfrage ging verloren, die Uhr holt ohne REQUEST nach.
  const w = world(telefonMitPlan());
  w.fire('appmessage', { payload: meldung(LEER, false) });
  check('leerer Uhrplan ohne Anfrage: der gespeicherte Plan bleibt',
        namen(w.planOnPhone()) === 'Kreatin,Vitamin D3,Black Maca', namen(w.planOnPhone() || []));
  check('leerer Uhrplan ohne Anfrage: die Uhr bekommt den Plan',
        w.sent.length === 1 && namen(w.sent[0].msg.PLAN) === 'Kreatin,Vitamin D3,Black Maca',
        w.sent.length);
  const clay = JSON.parse(w.store['clay-settings'] || '{}');
  check('leerer Uhrplan ohne Anfrage: die Konfigseite bleibt, wie sie war', clay.NAME1 === undefined,
        JSON.stringify(clay));
}
{
  const w = world(telefonMitPlan());
  w.fire('appmessage', { payload: meldung(UHR, false) });
  check('Uhr mit Plan ohne Anfrage: ihr Plan gilt', namen(w.planOnPhone()) === 'Zink',
        namen(w.planOnPhone() || []));
  check('Uhr mit Plan ohne Anfrage: nichts geht hinaus', w.sent.length === 0, w.sent.length);
}
{
  const s = telefonMitPlan();
  s.supcycle_pending = '1';
  const w = world(s);
  w.fire('appmessage', { payload: meldung(UHR, false) });
  check('offene Aenderung ohne Anfrage: der Plan der Uhr wird nicht uebernommen',
        namen(w.planOnPhone()) === 'Kreatin,Vitamin D3,Black Maca', namen(w.planOnPhone() || []));
}
{
  const w = world({ supcycle_plan: JSON.stringify(LEER) });
  w.fire('appmessage', { payload: meldung(LEER, false) });
  check('beide leer ohne Anfrage: nichts geht hinaus', w.sent.length === 0, w.sent.length);
}

console.log('\nDer ganze Ablauf aus H3');
{
  // Konfigseite speichert, die Uhr bestaetigt. Dann eine frische Uhr: ihre
  // Startanfrage wird abgelehnt (pkjs lief noch nicht), beim Nachholen kommt
  // der leere Plan OHNE REQUEST. Danach startet die App normal.
  const w = world({});
  w.fire('webviewclosed', { response: JSON.stringify({
    COUNT: '2', NAME1: 'Kreatin', TIME1: '480', NAME2: 'Vitamin D3', TIME2: '480' }) });
  if (w.sent[0]) w.sent[0].ok();
  const vorher = namen(w.planOnPhone());
  w.fire('appmessage', { payload: meldung(LEER, false) });
  check('Nachholen ohne Anfrage loescht nichts', namen(w.planOnPhone()) === vorher, namen(w.planOnPhone()) + ' statt ' + vorher);
  w.fire('appmessage', { payload: meldung(LEER, true) });
  const letzte = (w.sent[w.sent.length - 1] || { msg: { PLAN: [] } }).msg;
  check('beim naechsten Start bekommt die Uhr den Plan', namen(letzte.PLAN) === 'Kreatin,Vitamin D3',
        namen(letzte.PLAN));
}

console.log('\nDie Frage der Uhr nach der Zeit (M10)');
{
  // Die Uhr steht hinter ihrem gemerkten Tag und fragt (src/c/phone.c). Die
  // Antwort traegt NUR die Zeit des Telefons, in Sekunden seit 1970 -
  // entscheiden tut die Uhr.
  const w = world(telefonMitPlan());
  const vor = Math.floor(Date.now() / 1000);
  w.fire('appmessage', { payload: meldung(UHR, true, { UHRZEIT: 1791014400 }) });
  const nach = Math.floor(Date.now() / 1000);
  const antwort = w.sent.filter((x) => x.msg.UHRZEIT !== undefined);
  check('genau eine Antwort mit UHRZEIT', antwort.length === 1, w.sent.length);
  const a = antwort[0] ? antwort[0].msg : {};
  check('sie traegt nur die Zeit', Object.keys(a).join(',') === 'UHRZEIT', Object.keys(a).join(','));
  check('die Zeit des Telefons in Sekunden', a.UHRZEIT >= vor && a.UHRZEIT <= nach, a.UHRZEIT + ' nicht in ' + vor + '..' + nach);
  if (antwort[0]) antwort[0].ok();
  check('zugestellt: steht im Log', w.logs.join('|').indexOf('Zeit an die Uhr') >= 0, w.logs.join('|'));
}
{
  const w = world(telefonMitPlan());
  w.fire('appmessage', { payload: meldung(UHR, true) });
  check('ohne Frage: keine Zeit', w.sent.every((x) => x.msg.UHRZEIT === undefined), w.sent.length);
}

console.log('\nKein catch ohne Log');
{
  // Ein Telefonspeicher, der beim Lesen wirft: die Seite laeuft weiter, und
  // der Fehler steht im Log statt zu verschwinden.
  const w = world(telefonMitPlan());
  const kaputt = new Error('Speicher kaputt');
  const s = w.store;
  Object.defineProperty(s, 'supcycle_plan', { get() { throw kaputt; }, enumerable: true, configurable: true });
  let geworfen = null;
  try { w.fire('appmessage', { payload: meldung(UHR, true) }); } catch (e) { geworfen = e; }
  check('kein Absturz', geworfen === null, String(geworfen));
  check('der Fehler steht im Log', w.logs.join('|').indexOf('Fehler (Plan lesen): Error: Speicher kaputt') >= 0,
        w.logs.join('|'));
}

console.log('\nNamen als UTF-8 (hoechstens 15 Byte, nie mitten im Zeichen)');
{
  // Was die Uhr bekommt, Byte fuer Byte. Erwartet ist der Name, von Hand an
  // der letzten ganzen Zeichengrenze vor 15 Byte gekuerzt, mit Nullen
  // aufgefuellt - aus Buffer, nicht aus index.js.
  function erwartet(text) {
    const b = Array.from(Buffer.from(text, 'utf8'));
    while (b.length < 16) b.push(0);
    return b;
  }
  const faelle = [
    // [eingegeben, was auf der Uhr steht]
    ['Äpfelsäure Öl Üb', 'Äpfelsäure Ö'],       // 15 Byte genau, Umlaut am Ende ganz
    ['abcdefghijklmnä', 'abcdefghijklmn'],     // das ä haette Byte 15 und 16
    ['Zink 😀', 'Zink 😀'],                     // Emoji: ein 4-Byte-Zeichen
    ['Magnesium 1😀', 'Magnesium 1😀'],         // 11 + 4 = 15, passt genau
    ['Magnesium 12😀', 'Magnesium 12'],         // 12 + 4 = 16, Emoji faellt ganz
    ['Ein\uD800zel', 'Ein�zel'],           // einzelnes Ersatzzeichen
  ];
  faelle.forEach(([ein, aus]) => {
    const w = world({});
    w.fire('webviewclosed', { response: JSON.stringify({ COUNT: '1', NAME1: ein, TIME1: '480' }) });
    const b = w.sent[0] ? w.sent[0].msg.PLAN.slice(0, 16) : [];
    check('"' + ein + '" -> "' + aus + '"', JSON.stringify(b) === JSON.stringify(erwartet(aus)),
          JSON.stringify(b) + ' statt ' + JSON.stringify(erwartet(aus)));
    // Und zurueck: die Uhr bestaetigt, meldet ihn wieder, und die
    // Telefonseite liest denselben Namen.
    if (w.sent[0]) w.sent[0].ok();
    w.fire('appmessage', { payload: meldung(w.sent[0] ? w.sent[0].msg.PLAN : LEER, false) });
    const clay = JSON.parse(w.store['clay-settings'] || '{}');
    check('"' + aus + '" kommt so zurueck', clay.NAME1 === aus, JSON.stringify(clay.NAME1));
  });
}

console.log('\nFehler: ' + fails);
process.exit(fails ? 1 : 0);
