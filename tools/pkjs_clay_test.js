// Praeparatnamen mit '$' und '<' durch die echte Konfigseite (Clay).
//
//   node tools/pkjs_clay_test.js        (braucht npm install: @rebble/clay)
//
// Was hier leicht falsch und teuer ist (Audit N8): Clay setzt die
// gespeicherten Werte mit String.replace in die Seite ein. '$&', "$'", '$`'
// und '$$' sind dort Ersetzungsmuster, und '</script>' beendet das Skript der
// Seite. Bis 0.15.0 war die Seite mit so einem Namen kaputt, und beim
// naechsten Speichern kam ein anderer Name zurueck.
//
// Gebaut wird mit dem ECHTEN Clay aus node_modules und der echten
// config.js; nur Pebble und der Telefonspeicher sind Attrappen. Aus der
// erzeugten Seite werden die Einstellungen so gelesen, wie die Seite sie
// liest: als JavaScript-Ausdruck zwischen "window.claySettings=" und dem
// naechsten Eintrag.
//
// Exitcode 0 = alles wie zugesagt.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const Module = require('module');

const SRC = path.join(__dirname, '..', 'src', 'pkjs', 'index.js');
const CFG = path.join(__dirname, '..', 'src', 'pkjs', 'config.js');

let fails = 0;
function check(name, ok, detail) {
  console.log((ok ? '  ok     ' : '  FEHLER ') + name + (ok ? '' : '   -> ' + detail));
  if (!ok) fails++;
}

// Clay verlangt 'message_keys', das sonst der Pebble-Bau erzeugt.
const ladeModul = Module._load;
Module._load = function (anfrage) {
  if (anfrage === 'message_keys') return {};
  return ladeModul.apply(this, arguments);
};
// Wie im Pebble-Bau: das Paket liefert dist/js/index.js als pkjs-Teil.
const Clay = Module.createRequire(SRC)('@rebble/clay/dist/js/index.js');

// Eine Welt: index.js mit dem echten Clay. Clay greift auf die globalen
// Pebble und localStorage zu - dieselben wie in der Welt von index.js.
function world(store, plattform) {
  store = store || {};
  const sent = [];
  const logs = [];
  let url = null;
  const localStorage = {
    getItem: (k) => (k in store ? store[k] : null),
    setItem: (k, v) => { store[k] = String(v); },
    removeItem: (k) => { delete store[k]; },
  };
  const Pebble = {
    platform: plattform || 'basalt',
    addEventListener: (ev, fn) => { (sandbox.__ev[ev] = sandbox.__ev[ev] || []).push(fn); },
    sendAppMessage: (msg, ok, nok) => { sent.push({ msg: msg, ok: ok, nok: nok }); },
    openURL: (u) => { url = u; },
    getActiveWatchInfo: () => ({ platform: 'emery', model: 'x', language: 'de_DE',
                                 firmware: { major: 4, minor: 4, patch: 0, suffix: '' } }),
    getAccountToken: () => '',
    getWatchToken: () => '',
  };
  global.localStorage = localStorage;
  global.Pebble = Pebble;
  const sandbox = {
    console: { log: (m) => logs.push(String(m)) },
    Date, Math, JSON, parseInt, parseFloat, isNaN, isFinite, encodeURIComponent, decodeURIComponent,
    escape, String, Number, Object, Array, setTimeout, clearTimeout,
    XMLHttpRequest: function () {},
    localStorage, Pebble, __ev: {},
  };
  sandbox.module = { exports: {} };
  sandbox.require = function (id) {
    if (id === '@rebble/clay') return Clay;
    if (id === './config') return require(CFG);
    return Module.createRequire(SRC)(id);
  };
  vm.createContext(sandbox);
  vm.runInContext(fs.readFileSync(SRC, 'utf8'), sandbox, { filename: SRC });
  return {
    store, sent, logs, url: () => url,
    fire: (ev, arg) => (sandbox.__ev[ev] || []).forEach((fn) => fn(arg)),
  };
}

// Ein Plan der Uhr mit einem Namen auf Platz 1 (26 Byte je Platz, plan.h).
function uhrPlan(name) {
  const b = new Array(156).fill(0);
  Buffer.from(name, 'utf8').forEach((x, i) => { if (i < 15) b[i] = x; });
  b[16] = 8; b[18] = 1; b[19] = 1;
  return b;
}

// Die Seite aus der URL, und was die Seite als Einstellungen liest.
function seite(url) {
  const i = url.indexOf(url.indexOf('data:') === 0 ? ',' : '#');
  return decodeURIComponent(url.slice(i + 1));
}
function einstellungen(html) {
  const a = html.indexOf('window.claySettings=');
  const e = html.indexOf(',window.customFn=', a);
  if (a < 0 || e < 0) return null;
  try { return vm.runInNewContext('(' + html.slice(a + 'window.claySettings='.length, e) + ')'); } catch (err) { return null; }
}
const zaehle = (text, was) => text.split(was).length - 1;

// Grundlinie: ein gewoehnlicher Name.
function seiteFuer(name, plattform) {
  const w = world({}, plattform);
  w.fire('appmessage', { payload: { PLAN: uhrPlan(name), FX: 1, TODAY: 20261004, DUE: 0, TAKEN: 0 } });
  const vorher = w.store['clay-settings'];
  w.fire('showConfiguration');
  return { w, html: seite(w.url() || ''), vorher };
}
const grund = seiteFuer('Zink').html;
const SKRIPTE = zaehle(grund, '</script>');

console.log('\nNamen durch die Seite (N8)');
['a$&b', "a$'b", 'a$`b', 'a$$b', '$$META$$', 'x</script>', '</SCRIPT>y', 'Zink<3', 'Öl & $5'].forEach((name) => {
  const { w, html, vorher } = seiteFuer(name);
  const s = einstellungen(html);
  check(JSON.stringify(name) + ': die Seite zeigt den Namen unveraendert', s && s.NAME1 === name,
        s ? JSON.stringify(s.NAME1) : 'Einstellungen nicht lesbar');
  check(JSON.stringify(name) + ': die Seite ist ganz (Skripte, Laenge)',
        zaehle(html, '</script>') === SKRIPTE && Math.abs(html.length - grund.length) < 200,
        zaehle(html, '</script>') + ' Skripte, ' + html.length + ' statt ' + grund.length + ' Zeichen');
  check(JSON.stringify(name) + ': der Telefonspeicher bleibt, wie er war', w.store['clay-settings'] === vorher,
        w.store['clay-settings']);
  // Und zurueck: die Seite schickt den Namen, so wie Clay ihn verpackt.
  const antwort = encodeURIComponent(JSON.stringify(Object.assign({}, s, { NAME1: { value: name } })));
  w.fire('webviewclosed', { response: antwort });
  const b = w.sent.length ? w.sent[w.sent.length - 1].msg.PLAN.slice(0, 16) : [];
  const soll = Array.from(Buffer.from(name, 'utf8')).slice(0, 15);
  check(JSON.stringify(name) + ': an die Uhr geht derselbe Name', JSON.stringify(b.slice(0, soll.length)) === JSON.stringify(soll),
        Buffer.from(b).toString('utf8'));
});
{
  // Im Emulator haengt Clay die Seite anders an - die Marke findet sich auch dort.
  const { html } = seiteFuer('x</script>', 'pypkjs');
  const s = einstellungen(html);
  check('Emulator: auch dort unveraendert', s && s.NAME1 === 'x</script>' && zaehle(html, '</script>') === SKRIPTE,
        s ? JSON.stringify(s.NAME1) : 'nicht lesbar');
}

console.log('\nAbgebrochene Seite');
{
  const w = world({});
  w.fire('appmessage', { payload: { PLAN: uhrPlan('Zink'), FX: 1, TODAY: 20261004, DUE: 0, TAKEN: 0 } });
  const vorher = JSON.stringify(w.store);
  let geworfen = null;
  try { w.fire('webviewclosed', { response: 'CANCELLED' }); } catch (e) { geworfen = e; }
  check('"CANCELLED" wirft nicht', geworfen === null, String(geworfen));
  check('nichts geaendert, nichts gesendet', JSON.stringify(w.store) === vorher && w.sent.length === 0, w.sent.length);
  check('es steht im Log', w.logs.some((l) => l.indexOf('Fehler (Antwort der Konfigseite)') >= 0), w.logs.join(' | '));
}

console.log('\nFehler: ' + fails);
process.exit(fails ? 1 : 0);
