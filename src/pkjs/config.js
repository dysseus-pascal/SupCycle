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
// In fuenf Sprachen wie die App. Welche Sprache gilt, sagt die UHR per LANG —
// das Telefon kann die Uhrsprache nicht von sich aus erfahren.

var SLOTS = 6;

// Spalte 0 ist ENGLISCH, wie in strings_table.h; danach Deutsch,
// Franzoesisch, Italienisch, Spanisch - dieselben Nummern, die die Uhr als
// LANG schickt.
var TEXT = [
  {
    heading: 'SupCycle',
    intro: 'What you take, when, and in which cycle. A slot without a name ' +
           'stays empty.',
    count: 'How many supplements?',
    slot: 'Supplement',
    name: 'Name',
    namePlaceholder: 'e.g. multivitamin',
    time: 'When',
    fx: 'Animation when ticking off',
    fxHint: 'Pilly the capsule plays once everything at one time is done',
    every: 'Every how many days',
    everyHint: '1 = daily',
    weeksOn: 'Weeks on',
    weeksOnHint: 'empty = unlimited',
    weeksOff: 'Weeks off',
    weeksOffHint: 'empty = none',
    since: 'Cycle running for',
    sinceNow: 'starts today',
    sinceWeeks: function (n) { return n === 1 ? 'one week' : n + ' weeks'; },
    cycleNote: 'Only when weeks are entered. During the break the watch stays quiet — ' +
               'otherwise you tick it off out of habit and the cycle is moot.',
    submit: 'Save'
  },
  {
    heading: 'SupCycle',
    intro: 'Was du nimmst, wann, und in welchem Zyklus. Ein Platz ohne Namen ' +
           'bleibt leer.',
    count: 'Wie viele Präparate?',
    slot: 'Präparat',
    name: 'Name',
    namePlaceholder: 'z. B. Multivitamin',
    time: 'Wann',
    fx: 'Animation beim Abhaken',
    fxHint: 'Pilly, die Kapsel, spielt wenn alles zu einer Uhrzeit erledigt ist',
    every: 'Alle wie viel Tage',
    everyHint: '1 = täglich',
    weeksOn: 'Wochen Einnahme',
    weeksOnHint: 'leer = unbegrenzt',
    weeksOff: 'Wochen Pause',
    weeksOffHint: 'leer = keine',
    since: 'Zyklus läuft seit',
    sinceNow: 'beginnt heute',
    sinceWeeks: function (n) { return n === 1 ? 'einer Woche' : n + ' Wochen'; },
    cycleNote: 'Nur bei eingetragenen Wochen. In der Pause erinnert die Uhr nicht — ' +
               'sonst hakst du aus Gewohnheit ab, und der Zyklus ist wertlos.',
    submit: 'Speichern'
  },
  {
    heading: 'SupCycle',
    intro: 'Ce que tu prends, quand, et selon quel cycle. Un emplacement ' +
           'sans nom reste vide.',
    count: 'Combien de compléments\u00a0?',
    slot: 'Complément',
    name: 'Nom',
    namePlaceholder: 'p. ex. multivitamine',
    time: 'Quand',
    fx: 'Animation en cochant',
    fxHint: 'Pilly, la gélule, s\u2019anime quand tout est pris pour une heure donnée',
    every: 'Tous les combien de jours',
    everyHint: '1 = tous les jours',
    weeksOn: 'Semaines de prise',
    weeksOnHint: 'vide = illimité',
    weeksOff: 'Semaines de pause',
    weeksOffHint: 'vide = aucune',
    since: 'Cycle en cours depuis',
    sinceNow: 'commence aujourd\u2019hui',
    sinceWeeks: function (n) { return n === 1 ? 'une semaine' : n + ' semaines'; },
    cycleNote: 'Seulement si des semaines sont saisies. Pendant la pause, la ' +
               'montre ne rappelle rien — sinon tu cocherais par habitude, et ' +
               'le cycle ne servirait à rien.',
    submit: 'Enregistrer'
  },
  {
    heading: 'SupCycle',
    intro: 'Cosa prendi, quando e con quale ciclo. Un posto senza nome ' +
           'resta vuoto.',
    count: 'Quanti integratori?',
    slot: 'Integratore',
    name: 'Nome',
    namePlaceholder: 'es. multivitaminico',
    time: 'Quando',
    fx: 'Animazione alla spunta',
    fxHint: 'Pilly, la capsula, si anima quando tutto è preso per un orario',
    every: 'Ogni quanti giorni',
    everyHint: '1 = ogni giorno',
    weeksOn: 'Settimane di assunzione',
    weeksOnHint: 'vuoto = illimitato',
    weeksOff: 'Settimane di pausa',
    weeksOffHint: 'vuoto = nessuna',
    since: 'Ciclo in corso da',
    sinceNow: 'inizia oggi',
    sinceWeeks: function (n) { return n === 1 ? 'una settimana' : n + ' settimane'; },
    cycleNote: 'Solo se sono inserite delle settimane. Durante la pausa ' +
               'l\u2019orologio non ricorda nulla — altrimenti spunteresti per ' +
               'abitudine e il ciclo non servirebbe a niente.',
    submit: 'Salva'
  },
  {
    heading: 'SupCycle',
    intro: 'Qué tomas, cuándo y con qué ciclo. Un hueco sin nombre se ' +
           'queda vacío.',
    count: '¿Cuántos suplementos?',
    slot: 'Suplemento',
    name: 'Nombre',
    namePlaceholder: 'p. ej. multivitamínico',
    time: 'Cuándo',
    fx: 'Animación al marcar',
    fxHint: 'Pilly, la cápsula, se anima cuando todo lo de una hora está tomado',
    every: 'Cada cuántos días',
    everyHint: '1 = a diario',
    weeksOn: 'Semanas de toma',
    weeksOnHint: 'vacío = sin límite',
    weeksOff: 'Semanas de pausa',
    weeksOffHint: 'vacío = ninguna',
    since: 'Ciclo en marcha desde hace',
    sinceNow: 'empieza hoy',
    sinceWeeks: function (n) { return n === 1 ? 'una semana' : n + ' semanas'; },
    cycleNote: 'Solo si hay semanas anotadas. Durante la pausa el reloj no ' +
               'avisa — si no, marcarías por costumbre y el ciclo no serviría ' +
               'de nada.',
    submit: 'Guardar'
  }
];

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
    // Eine Kennung AUF DER SECTION brächte nichts: Clay macht daraus nur ein
    // <div class="section"> und verwirft die id. Sie hängt deshalb an der
    // Überschrift, und die Vorwahl findet den Kasten von dort aus.
    items: [
      { type: 'heading', id: 'head' + n, defaultValue: t.slot + ' ' + n },
      {
        type: 'input',
        messageKey: 'NAME' + n,
        label: t.name,
        attributes: { placeholder: t.namePlaceholder, limit: 14 }
      },
      {
        type: 'select',
        messageKey: 'TIME' + n,
        label: t.time,
        defaultValue: '480',   // 08:00
        options: timeOptions()
      },
      // Drei Zahlenfelder statt einer Rhythmus-Auswahl. Ob etwas zyklisch
      // ist, steht damit nicht mehr als eigene Frage da - es ergibt sich
      // daraus, ob Wochen eingetragen sind. Eine Auswahl, deren Antwort in
      // den Feldern darunter noch einmal steht, kann widersprüchlich werden.
      {
        type: 'input',
        messageKey: 'EVERY' + n,
        label: t.every,
        description: t.everyHint,
        attributes: { type: 'number', min: 1, max: 30, placeholder: '1' }
      },
      {
        type: 'input',
        messageKey: 'ON' + n,
        label: t.weeksOn,
        description: t.weeksOnHint,
        attributes: { type: 'number', min: 1, max: 52, placeholder: t.weeksOnHint }
      },
      {
        type: 'input',
        messageKey: 'OFF' + n,
        label: t.weeksOff,
        description: t.weeksOffHint,
        attributes: { type: 'number', min: 1, max: 52, placeholder: t.weeksOffHint }
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
    { type: 'text', defaultValue: t.intro },
    {
      // Vorwahl: so viele Plätze zeigt die Seite, der Rest bleibt verborgen.
      // Ohne sie stünden hier immer sechs Abschnitte, von denen die meisten
      // leer bleiben - und man sucht seinen Eintrag zwischen Platzhaltern.
      type: 'select',
      messageKey: 'COUNT',
      label: t.count,
      defaultValue: '2',
      options: numberOptions(1, SLOTS)
    },
    {
      // Ganz oben und nicht je Platz: die Animation gehört zur App, nicht
      // zum einzelnen Präparat.
      type: 'toggle',
      messageKey: 'FX',
      label: t.fx,
      description: t.fxHint,
      defaultValue: true
    }
  ];
  for (var i = 0; i < SLOTS; i++) page.push(slotSection(t, i));
  page.push({ type: 'text', defaultValue: t.cycleNote });
  page.push({ type: 'submit', defaultValue: t.submit });
  return page;
};

module.exports.SLOTS = SLOTS;

/**
 * Läuft IN DER KONFIGSEITE, nicht hier: Clay reicht diese Funktion in die
 * Webansicht weiter. Sie darf deshalb nichts von aussen benutzen - keine
 * Variable dieser Datei, keinen Verweis auf SLOTS. Was sie braucht, steht in
 * ihr selbst.
 *
 * Aufgabe: nur so viele Plätze zeigen, wie die Vorwahl sagt, und sofort
 * nachziehen, wenn man sie ändert.
 */
module.exports.custom = function () {
  var clayConfig = this;
  var MAX = 6;
  var KEYS = ['NAME', 'TIME', 'EVERY', 'ON', 'OFF', 'SINCE'];

  // Warum nicht einfach getItemById('slot1'):
  //
  // Clay legt beim Aufbau NUR Nicht-Section-Elemente in sein Verzeichnis. Eine
  // section wird zu einem blossen <div class="section">, ihre id fällt dabei
  // weg. getItemById('slot1') gab also immer undefined zurück - und weil die
  // Schleife das stillschweigend übersprang, tat die Vorwahl gar nichts.
  //
  // Die Überschrift dagegen BEHÄLT ihre Kennung. Von ihrem Element aus lässt
  // sich der umgebende Kasten finden und verbergen. Nur die Felder zu
  // verbergen genügte nicht: .section hat eigenen Hintergrund und Schatten und
  // bliebe als leerer grauer Kasten stehen.
  function box(i) {
    var head = clayConfig.getItemById('head' + i);
    if (!head || !head.$element || !head.$element[0]) return null;
    var el = head.$element[0];
    return el.closest ? el.closest('.section') : null;
  }

  function apply() {
    var sel = clayConfig.getItemByMessageKey('COUNT');
    var n = sel ? parseInt(sel.get(), 10) : MAX;
    if (!n || n < 1 || n > MAX) n = MAX;

    for (var i = 1; i <= MAX; i++) {
      var on = i <= n;
      var b = box(i);
      if (b) {
        if (on) b.classList.remove('hide');
        else b.classList.add('hide');
        continue;
      }
      // Kein Kasten gefunden - dann wenigstens die Felder selbst. Ein leerer
      // Rahmen zu viel ist besser als sechs Abschnitte, die niemand wollte.
      var head = clayConfig.getItemById('head' + i);
      if (head) { if (on) head.show(); else head.hide(); }
      for (var k = 0; k < KEYS.length; k++) {
        var it = clayConfig.getItemByMessageKey(KEYS[k] + i);
        if (!it) continue;
        if (on) it.show(); else it.hide();
      }
    }
  }

  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    apply();
    var sel = clayConfig.getItemByMessageKey('COUNT');
    if (sel) sel.on('change', apply);
  });
};
