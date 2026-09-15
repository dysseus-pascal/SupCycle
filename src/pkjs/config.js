// Konfigurationsseite für die Telefon-App (Clay).
//
// Hier steht der ganze Plan: welche Präparate, wann, und mit welchem Zyklus.
// Die Uhr bekommt ihn fertig geschickt und rechnet nur noch aus, was heute
// ansteht — sie weiss nichts von Eingabefeldern.
//
// SECHS FESTE PLÄTZE statt einer Liste zum Anlegen und Löschen. Clay kennt
// keine dynamischen Listen; sie nachzubauen hiesse, die Seite selbst zu
// schreiben. Sechs Zeilen decken jeden Stack ab, den man von Hand pflegt, und
// ein Platz ohne Namen zählt einfach nicht.
//
// Zweisprachig wie die App. Welche Sprache gilt, sagt die UHR per LANG —
// das Telefon kann die Uhrsprache nicht von sich aus erfahren.

var SLOTS = 6;

var TEXT = [
  {
    heading: 'Kurintervall',
    intro: 'Was du nimmst, wann, und in welchem Zyklus. Ein Platz ohne Namen ' +
           'bleibt leer.',
    slot: 'Präparat',
    name: 'Name',
    namePlaceholder: 'z. B. Multivitamin',
    time: 'Wann',
    mode: 'Rhythmus',
    modeOff: 'nicht benutzt',
    modeDaily: 'täglich',
    modeCyclic: 'zyklisch',
    weeksOn: 'Wochen Einnahme',
    weeksOff: 'Wochen Pause',
    since: 'Zyklus läuft seit',
    sinceNow: 'beginnt heute',
    sinceWeeks: function (n) { return n === 1 ? 'einer Woche' : n + ' Wochen'; },
    cycleNote: 'Nur bei "zyklisch". In der Pause erinnert die Uhr nicht — ' +
               'sonst hakst du aus Gewohnheit ab, und der Zyklus ist wertlos.',
    submit: 'Speichern'
  },
  {
    heading: 'Kurintervall',
    intro: 'What you take, when, and in which cycle. A slot without a name ' +
           'stays empty.',
    slot: 'Supplement',
    name: 'Name',
    namePlaceholder: 'e.g. multivitamin',
    time: 'When',
    mode: 'Rhythm',
    modeOff: 'unused',
    modeDaily: 'daily',
    modeCyclic: 'cyclic',
    weeksOn: 'Weeks on',
    weeksOff: 'Weeks off',
    since: 'Cycle running for',
    sinceNow: 'starts today',
    sinceWeeks: function (n) { return n === 1 ? 'one week' : n + ' weeks'; },
    cycleNote: 'Only for "cyclic". During the break the watch stays quiet — ' +
               'otherwise you tick it off out of habit and the cycle is moot.',
    submit: 'Save'
  }
];
// Index 0 ist Deutsch? Nein: Spalte 0 ist ENGLISCH, wie in strings_table.h.
// Deshalb hier umdrehen, damit TEXT[0] zu lang 0 passt.
TEXT = [TEXT[1], TEXT[0]];

// Halbstundenraster von 5 bis 23 Uhr. Feiner wäre eine Scheingenauigkeit —
// niemand nimmt sein Multivitamin um 07:47.
function timeOptions() {
  var out = [];
  for (var h = 5; h <= 23; h++) {
    for (var m = 0; m < 60; m += 30) {
      var label = (h < 10 ? '0' : '') + h + ':' + (m === 0 ? '00' : '30');
      out.push({ label: label, value: String(h * 60 + m) });
    }
  }
  return out;
}

function numberOptions(from, to, fmt) {
  var out = [];
  for (var n = from; n <= to; n++) {
    out.push({ label: fmt ? fmt(n) : String(n), value: String(n) });
  }
  return out;
}

function slotSection(t, i) {
  var n = i + 1;
  return {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: t.slot + ' ' + n },
      {
        type: 'input',
        messageKey: 'NAME' + n,
        label: t.name,
        attributes: { placeholder: t.namePlaceholder, limit: 14 }
      },
      {
        type: 'select',
        messageKey: 'MODE' + n,
        label: t.mode,
        defaultValue: '0',
        options: [
          { label: t.modeOff, value: '0' },
          { label: t.modeDaily, value: '1' },
          { label: t.modeCyclic, value: '2' }
        ]
      },
      {
        type: 'select',
        messageKey: 'TIME' + n,
        label: t.time,
        defaultValue: '480',   // 08:00
        options: timeOptions()
      },
      {
        type: 'select',
        messageKey: 'ON' + n,
        label: t.weeksOn,
        defaultValue: '8',
        options: numberOptions(1, 26)
      },
      {
        type: 'select',
        messageKey: 'OFF' + n,
        label: t.weeksOff,
        defaultValue: '2',
        options: numberOptions(1, 12)
      },
      {
        type: 'select',
        messageKey: 'SINCE' + n,
        label: t.since,
        defaultValue: '0',
        options: numberOptions(0, 25, function (w) {
          return w === 0 ? t.sinceNow : t.sinceWeeks(w);
        })
      }
    ]
  };
}

module.exports = function (lang) {
  var t = TEXT[lang] || TEXT[0];
  var page = [
    { type: 'heading', defaultValue: t.heading },
    { type: 'text', defaultValue: t.intro }
  ];
  for (var i = 0; i < SLOTS; i++) page.push(slotSection(t, i));
  page.push({ type: 'text', defaultValue: t.cycleNote });
  page.push({ type: 'submit', defaultValue: t.submit });
  return page;
};

module.exports.SLOTS = SLOTS;
