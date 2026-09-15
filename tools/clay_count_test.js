// Prueft die Anzahlsvorwahl der Konfigseite.
//
//   node tools/clay_count_test.js
//
// WARUM ES DIESEN TEST GIBT: die erste Fassung versteckte gar nichts, und zwar
// STILL. Sie fragte clayConfig.getItemById('slot1') nach der section und
// uebersprang das Ergebnis, wenn es leer war - es war immer leer. Clay legt
// beim Aufbau naemlich nur NICHT-section-Elemente in sein Verzeichnis; aus
// einer section wird ein blosses <div class="section">, ihre id faellt weg.
// (node_modules/@rebble/clay/src/scripts/lib/clay-config.js, _addItems.)
//
// Der Nachbau hier bildet genau das ab: getItemById kennt Ueberschriften, aber
// KEINE sections. Wer den alten Weg wieder einbaut, faellt hier durch statt
// erst auf dem Telefon.
//
// GRENZE: geprueft wird die Logik der custom-Funktion, nicht Clays Darstellung.
// Dass die Klasse "hide" verbirgt, steht im Stylesheet der erzeugten Seite
// (.hide{display:none!important}) und wird hier vorausgesetzt.
'use strict';

const path = require('path');
const config = require(path.join(__dirname, '..', 'src', 'pkjs', 'config.js'));

const MAX = 6;
const KEYS = ['NAME', 'TIME', 'EVERY', 'ON', 'OFF', 'SINCE'];
let fails = 0;

function check(name, ok, info) {
  if (ok) {
    console.log('  ok     ' + name);
  } else {
    fails++;
    console.log('  FEHLER ' + name + (info ? '   -> ' + info : ''));
  }
}

// Ein Nachbau von Clay, so genau wie noetig: Kaesten, Ueberschriften mit
// Kennung, Felder mit Nachrichtenschluessel - und sections OHNE Kennung.
function fakeClay(count) {
  const boxes = [];
  const byId = {};
  const byKey = {};
  const shown = {};

  for (let i = 1; i <= MAX; i++) {
    const cls = new Set();
    const box = {
      classList: {
        add: (c) => cls.add(c),
        remove: (c) => cls.delete(c),
        contains: (c) => cls.has(c),
      },
    };
    boxes.push(box);
    byId['head' + i] = {
      $element: [{ closest: (sel) => (sel === '.section' ? box : null) }],
      show: () => { shown['head' + i] = true; },
      hide: () => { shown['head' + i] = false; },
    };
    KEYS.forEach((k) => {
      byKey[k + i] = {
        show: () => { shown[k + i] = true; },
        hide: () => { shown[k + i] = false; },
      };
    });
  }

  let changeHandler = null;
  byKey.COUNT = {
    get: () => String(count),
    set: (v) => { count = v; if (changeHandler) changeHandler(); },
    on: (ev, fn) => { if (ev === 'change') changeHandler = fn; },
  };

  const clay = {
    EVENTS: { AFTER_BUILD: 'AFTER_BUILD' },
    // Genau wie Clay: sections stehen hier NICHT drin.
    getItemById: (id) => byId[id],
    getItemByMessageKey: (k) => byKey[k],
    on: (ev, fn) => { if (ev === 'AFTER_BUILD') clay._afterBuild = fn; },
    _afterBuild: null,
  };
  return {
    clay, boxes, shown,
    build: () => clay._afterBuild && clay._afterBuild(),
    setCount: (v) => byKey.COUNT.set(v),
    hidden: () => boxes.map((b) => b.classList.contains('hide')),
  };
}

function run(count) {
  const w = fakeClay(count);
  config.custom.call(w.clay);
  w.build();
  return w;
}

console.log('Anzahlsvorwahl auf der Konfigseite');
console.log('');

console.log('Zwei vorgewaehlt');
{
  const w = run('2');
  const h = w.hidden();
  check('Platz 1 und 2 bleiben sichtbar', h[0] === false && h[1] === false,
        JSON.stringify(h));
  check('Platz 3 bis 6 sind verborgen', h.slice(2).every((x) => x === true),
        JSON.stringify(h));
}

console.log('');
console.log('Alle sechs');
{
  const w = run('6');
  check('kein Platz ist verborgen', w.hidden().every((x) => x === false),
        JSON.stringify(w.hidden()));
}

console.log('');
console.log('Einer');
{
  const w = run('1');
  const h = w.hidden();
  check('nur der erste bleibt', h[0] === false && h.slice(1).every((x) => x === true),
        JSON.stringify(h));
}

console.log('');
console.log('Unsinnige Vorwahl');
{
  // Lieber alles zeigen als alles verstecken: eine leere Seite saehe aus wie
  // ein Fehler, sechs Plaetze nur wie eine ungenutzte Vorwahl.
  const w = run('');
  check('leer zeigt alle', w.hidden().every((x) => x === false), JSON.stringify(w.hidden()));
  const w2 = run('99');
  check('zu gross zeigt alle', w2.hidden().every((x) => x === false), JSON.stringify(w2.hidden()));
  const w3 = run('0');
  check('null zeigt alle', w3.hidden().every((x) => x === false), JSON.stringify(w3.hidden()));
}

console.log('');
console.log('Aenderung wirkt sofort');
{
  const w = run('4');
  check('zuerst vier sichtbar', w.hidden().filter((x) => !x).length === 4,
        JSON.stringify(w.hidden()));
  w.setCount('2');
  check('nach dem Umstellen zwei sichtbar', w.hidden().filter((x) => !x).length === 2,
        JSON.stringify(w.hidden()));
  w.setCount('5');
  check('und wieder hoch auf fuenf', w.hidden().filter((x) => !x).length === 5,
        JSON.stringify(w.hidden()));
}

console.log('');
console.log('Ohne Kasten bleibt der Rueckfall');
{
  // Fände die custom-Funktion den Kasten nicht - andere Clay-Fassung, anderes
  // Markup -, muss sie wenigstens die Felder selbst verbergen. Ein leerer
  // Rahmen zu viel ist besser als sechs Abschnitte, die niemand wollte.
  const w = fakeClay('2');
  w.boxes.forEach((_, i) => {
    w.clay.getItemById('head' + (i + 1)).$element[0].closest = () => null;
  });
  config.custom.call(w.clay);
  w.build();
  check('Felder von Platz 3 sind verborgen',
        w.shown.NAME3 === false && w.shown.TIME3 === false,
        JSON.stringify({ NAME3: w.shown.NAME3, TIME3: w.shown.TIME3 }));
  check('Felder von Platz 1 bleiben sichtbar',
        w.shown.NAME1 === true && w.shown.TIME1 === true,
        JSON.stringify({ NAME1: w.shown.NAME1, TIME1: w.shown.TIME1 }));
  check('auch die Ueberschrift von Platz 3 ist weg', w.shown.head3 === false,
        String(w.shown.head3));
}

console.log('');
console.log('Fehler: ' + fails);
process.exit(fails ? 1 : 0);
