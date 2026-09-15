// Die Timeline-Pins der Telefonseite.
//
//   node tools/pkjs_pin_test.js
//
// WARUM HIER UND NICHT IM EMULATOR: dort gibt es keine Konfigseite, also auch
// keinen gespeicherten Plan — die Telefonseite käme nie über "kein Plan"
// hinaus. Und auf der echten Uhr sieht man dem Pin nicht an, ob seine Kennung
// stimmt oder ob er beim nächsten Mal unnötig erneut hinausgeht.
//
// Drei Dinge sind hier leicht falsch und teuer:
//
//   - Die KENNUNG entscheidet, ob ein Pin ersetzt oder verdoppelt wird. Eine,
//     die den Tag nicht enthält, überschriebe morgen den von heute.
//   - Die SIGNATUR entscheidet, wann erneut gesendet wird. Wer den Zustand
//     vergisst, schickt einen abgehakten Pin nie als abgehakt hinaus; wer zu
//     viel hineinnimmt, sendet bei jeder Auffrischung alles neu.
//   - Der ZEITPUNKT muss der Kalendertag der UHR sein. Genau daran ist der
//     erste Entwurf gescheitert: die Uhr schickte eine Tagesnummer (Tage seit
//     der Epoche, aus ihrer Ortszeit gebildet), und das Telefon rechnete sie
//     über UTC zurück — bei positiver Zeitzone landete der Pin einen Tag zu
//     früh, also in der Vergangenheit. Jetzt kommt JJJJMMTT.
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

// Eine Welt: index.js frisch geladen, Telefonspeicher vorbelegbar, Timeline-
// Aufrufe abgefangen statt gesendet.
function world(store, opts) {
  store = store || {};
  opts = opts || {};
  const pins = [];
  const logs = [];

  function XHR() { this.status = 200; }
  XHR.prototype.open = function (m, u) { this._m = m; this._u = u; };
  XHR.prototype.setRequestHeader = function () {};
  XHR.prototype.send = function (body) {
    const self = this;
    pins.push({ method: this._m, id: this._u.split('/').pop(), body: JSON.parse(body) });
    setTimeout(function () { self.status = 200; self.onload && self.onload.call(self); }, 0);
  };

  function FakeClay(config) {
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
    String, Number, Object, setTimeout, clearTimeout,
    XMLHttpRequest: XHR,
    localStorage: {
      getItem: (k) => (k in store ? store[k] : null),
      setItem: (k, v) => { store[k] = String(v); },
    },
    Pebble: {
      addEventListener: (ev, fn) => { (sandbox.__ev[ev] = sandbox.__ev[ev] || []).push(fn); },
      sendAppMessage: () => {},
      openURL: () => {},
      getTimelineToken: opts.noToken ? undefined
        : (ok) => setTimeout(() => ok('TESTTOKEN'), 0),
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
    store, pins, logs,
    fire: (ev, arg) => (sandbox.__ev[ev] || []).forEach((fn) => fn(arg)),
  };
}

// Grosszuegig warten: die Pins gehen nacheinander hinaus, jeder ueber einen
// eigenen Zeitgeber. Zu knapp bemessen misst man die Geduld des Pruefstands
// statt des Verhaltens - beim ersten Lauf fehlten so zwei Pins, die es
// laengst gab.
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const SETTLE = 250;

// Ein Plan wie der echte: zwei täglich um 8, einer mittags, einer abends.
function savePlan(w) {
  w.fire('webviewclosed', {
    response: JSON.stringify({
      NAME1: 'Multivitamin', MODE1: '1', TIME1: '480',
      NAME2: 'Kreatin', MODE2: '1', TIME2: '480',
      NAME3: 'Black Maca', MODE3: '2', TIME3: '750', ON3: '8', OFF3: '2', SINCE3: '2',
      NAME4: 'Ashwagandha', MODE4: '2', TIME4: '1200', ON4: '6', OFF4: '2', SINCE4: '6',
    }),
  });
}

// Datum wie die Uhr es schickt: JJJJMMTT aus der Ortszeit.
function ymdOf(d) {
  return d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
}
function todayNumber() {
  return ymdOf(new Date());
}
// Morgen richtig gerechnet, nicht DAY + 1: am Monatsende gaebe das einen
// Kalendertag, den es nicht gibt (20260931).
function tomorrowNumber() {
  const n = new Date();
  return ymdOf(new Date(n.getFullYear(), n.getMonth(), n.getDate() + 1));
}

async function main() {
  const DAY = todayNumber();

  console.log('\nOhne Plan');
  {
    const w = world();
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x0f, TAKEN: 0 } });
    await wait(SETTLE);
    check('keine Pins, und es steht im Log',
          w.pins.length === 0 && w.logs.some((l) => l.indexOf('kein Plan') >= 0),
          w.logs.join(' | '));
  }

  console.log('\nVier faellige Praeparate');
  let saved;
  {
    const w = world();
    savePlan(w);
    saved = JSON.parse(JSON.stringify(w.store));
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x0f, TAKEN: 0 } });
    await wait(SETTLE);
    check('vier Pins', w.pins.length === 4, w.pins.length + ': ' + w.pins.map((p) => p.id).join(', '));
    check('alle per PUT', w.pins.every((p) => p.method === 'PUT'), JSON.stringify(w.pins.map((p) => p.method)));

    const ids = w.pins.map((p) => p.id);
    check('Kennung traegt den Tag', ids.every((id) => /^supcycle-\d{8}-\d$/.test(id)), ids.join(', '));
    check('Kennungen sind verschieden', new Set(ids).size === 4, ids.join(', '));

    const titles = w.pins.map((p) => p.body.layout.title);
    check('Namen stehen im Titel',
          titles.indexOf('Multivitamin') >= 0 && titles.indexOf('Ashwagandha') >= 0,
          titles.join(', '));

    const mv = w.pins.filter((p) => p.body.layout.title === 'Multivitamin')[0];
    check('Symbol: faellig', mv.body.layout.tinyIcon.indexOf('NOTIFICATION_REMINDER') >= 0,
          mv.body.layout.tinyIcon);
    check('Symbol kommt aus dem System-Satz',
          w.pins.every((p) => p.body.layout.tinyIcon.indexOf('system://images/') === 0),
          'app:// erreicht die echte Uhr nicht');
    check('eine Aktion, oeffnet die App',
          mv.body.actions.length === 1 && mv.body.actions[0].type === 'openWatchApp',
          JSON.stringify(mv.body.actions));

    // Der Zeitpunkt muss der KALENDERTAG DER UHR zur Uhrzeit des Praeparats
    // sein. Um 08:00 eingetragen heisst 08:00 Ortszeit, nicht 08:00 UTC.
    const t = new Date(mv.body.time);
    check('Zeitpunkt: heute', ymdOf(t) === DAY, mv.body.time + ' -> ' + ymdOf(t) + ' statt ' + DAY);
    check('Zeitpunkt: 08:00 Ortszeit', t.getHours() === 8 && t.getMinutes() === 0,
          t.getHours() + ':' + t.getMinutes());

    const maca = w.pins.filter((p) => p.body.layout.title === 'Black Maca')[0];
    check('Black Maca um 12:30', new Date(maca.body.time).getHours() === 12 &&
          new Date(maca.body.time).getMinutes() === 30, maca.body.time);
  }

  console.log('\nNur was faellig ist');
  {
    const w = world(JSON.parse(JSON.stringify(saved)));
    // Ashwagandha (Platz 3, Bit 3) pausiert heute.
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x07, TAKEN: 0 } });
    await wait(SETTLE);
    check('drei Pins statt vier', w.pins.length === 3, String(w.pins.length));
    check('Ashwagandha ist nicht dabei',
          w.pins.every((p) => p.body.layout.title !== 'Ashwagandha'),
          w.pins.map((p) => p.body.layout.title).join(', '));
  }

  console.log('\nAbgehakt');
  {
    const w = world(JSON.parse(JSON.stringify(saved)));
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x0f, TAKEN: 0x01 } });
    await wait(SETTLE);
    const mv = w.pins.filter((p) => p.body.layout.title === 'Multivitamin')[0];
    check('genommenes traegt ein anderes Symbol',
          mv && mv.body.layout.tinyIcon.indexOf('GENERIC_CONFIRMATION') >= 0,
          mv && mv.body.layout.tinyIcon);
    check('und sagt es im Untertitel',
          mv && /genommen|taken/.test(mv.body.layout.subtitle), mv && mv.body.layout.subtitle);
  }

  console.log('\nNicht doppelt senden');
  {
    const w = world(JSON.parse(JSON.stringify(saved)));
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x0f, TAKEN: 0 } });
    await wait(SETTLE);
    const first = w.pins.length;
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x0f, TAKEN: 0 } });
    await wait(SETTLE);
    check('zweiter Durchlauf schickt nichts nach', w.pins.length === first,
          first + ' dann ' + w.pins.length);

    // Aber eine ECHTE Aenderung muss hinaus.
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x0f, TAKEN: 0x02 } });
    await wait(SETTLE);
    check('ein abgehaktes geht erneut hinaus', w.pins.length === first + 1,
          first + ' dann ' + w.pins.length);
    check('und zwar genau das abgehakte',
          w.pins[w.pins.length - 1].body.layout.title === 'Kreatin',
          w.pins[w.pins.length - 1].body.layout.title);
  }

  console.log('\nMorgen ist ein anderer Tag');
  {
    const w = world(JSON.parse(JSON.stringify(saved)));
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x01, TAKEN: 0 } });
    await wait(SETTLE);
    const heute = w.pins[0].id;
    w.fire('appmessage', { payload: { TODAY: tomorrowNumber(), DUE: 0x01, TAKEN: 0 } });
    await wait(SETTLE);
    const morgen = w.pins[w.pins.length - 1].id;
    check('andere Kennung als heute', heute !== morgen, heute + ' / ' + morgen);
    check('der Pin von morgen liegt einen Tag spaeter',
          new Date(w.pins[w.pins.length - 1].body.time) - new Date(w.pins[0].body.time) === 86400000,
          heute + ' -> ' + morgen);
  }

  console.log('\nOhne Timeline-Token');
  {
    const w = world(JSON.parse(JSON.stringify(saved)), { noToken: true });
    w.fire('appmessage', { payload: { TODAY: DAY, DUE: 0x01, TAKEN: 0 } });
    await wait(SETTLE);
    check('kein Absturz, und es steht im Log',
          w.logs.some((l) => l.indexOf('getTimelineToken') >= 0 || l.indexOf('lokale API') >= 0),
          w.logs.join(' | '));
  }

  console.log('\nFehler: ' + fails);
  process.exit(fails ? 1 : 0);
}

main();
