// SupCycle — Telefonseite.
//
// Ihre einzige Aufgabe: den auf der Konfigseite eingetragenen Plan in einen
// Datenblock packen und an die Uhr schicken. Die Uhr rechnet daraus selbst
// aus, was heute ansteht; sie fragt nie wieder nach.
//
// WARUM DER PLAN AUCH HIER LIEGT: die Konfigseite geht auch dann auf, wenn die
// App auf der UHR nicht läuft — und dann erreicht sie kein AppMessage. Der
// Block wird deshalb zusätzlich im Telefonspeicher gehalten und beim nächsten
// Start der Watchapp nachgereicht. Ohne diesen zweiten Weg verpufft jede
// Eingabe still. (Dieselbe Falle wie bei Drinktervall.)

var Clay = require('@rebble/clay');
var clayConfig = require('./config');

var SLOTS = clayConfig.SLOTS || 6;
var NAME_BYTES = 16;        // muss zu SC_NAME_LEN in src/c/plan.h passen
var ITEM_BYTES = 26;        // muss zu SC_ITEM_BYTES passen
var PLAN_KEY = 'supcycle_plan';
var ITEMS_KEY = 'supcycle_items';
var FX_KEY = 'supcycle_fx';       // '1' oder '0'; fehlt = an
var LANG_KEY = 'supcycle_lang';

// KEIN CATCH OHNE LOG. Ein Fehler beim Telefonspeicher oder beim Lesen soll im
// Log der Pebble-App stehen und nicht still verschwinden - sonst laesst sich
// ein verlorener Plan hinterher nicht erklaeren. tools/catch_check.js prueft,
// dass jeder catch ins Log meldet.
function meldeFehler(wo, e) {
  console.log('Fehler (' + wo + '): ' + e);
}

// Ein leerer Platz steht mit 0 im Byte 18. Frueher sagte dort ein Modus,
// ob taeglich oder zyklisch - das ergibt sich jetzt aus den Wochen.
var SLOT_UNUSED = 0, SLOT_USED = 1;

// --- Timeline ---
// Wie in Drinktervall: zuerst die Rebble-REST-Schnittstelle mit dem Token der
// Pebble-App, ohne Token die lokale Pebble.insertTimelinePin.
var API_URL = 'https://timeline-api.rebble.io/v1/user/pins/';
var PIN_COLOR = '#005555';                 // wie die Seitenleiste der App
var PIN_STORE = 'supcycle_pins_v1';        // id -> { sig, sentAt }
var RESEND_AFTER_MS = 12 * 3600 * 1000;    // unveraenderten Pin nach 12 h erneut
var FORGET_AFTER_MS = 3 * 86400 * 1000;

// Bei JEDER Aenderung am Aussehen erhoehen. Sonst bleiben schon gesendete
// Pins auf ihrem alten Stand stehen - ihr Zustand hat sich ja nicht geaendert.
var LOOK_VERSION = 2;

// Nur Namen aus dem System-Satz erreichen die echte Uhr: die Telefon-App setzt
// das Symbol ueber eine feste Tabelle, die ausschliesslich "system://images/..."
// kennt. Ein unbekannter Name wird stillschweigend weggelassen, und die Uhr
// zeichnet ihre Standardflagge.
var ICON_DUE = 'system://images/NOTIFICATION_REMINDER';
var ICON_TAKEN = 'system://images/GENERIC_CONFIRMATION';

// Spalte 0 ist Englisch, wie in strings_table.h; danach 1 Deutsch,
// 2 Franzoesisch, 3 Italienisch, 4 Spanisch (StringLang in src/c/strings.h).
var PIN_TEXT = [
  { taken: 'taken', open: 'Open app' },
  { taken: 'genommen', open: 'App öffnen' },
  { taken: 'pris', open: 'Ouvrir l\'app' },
  { taken: 'preso', open: 'Apri app' },
  { taken: 'tomado', open: 'Abrir app' }
];

// Eine Nummer, die diese Seite nicht kennt (eine neuere Uhr mit mehr
// Sprachen), bekommt Englisch statt einer leeren Seite.
function knownLang(v) {
  return (v >= 1 && v < PIN_TEXT.length) ? v : 0;
}

function getLang() {
  return knownLang(parseInt(localStorage.getItem(LANG_KEY), 10));
}

// Clay erst bauen, wenn die Seite gebraucht wird: dann steht die Sprache der
// Uhr schon fest. Eine gebaute Instanz bleibt, damit showConfiguration und
// webviewclosed dieselbe benutzen.
var s_clay = null;
function getClay() {
  if (!s_clay) {
    // Die zweite Stelle ist die Funktion, die IN der Konfigseite laeuft: sie
    // blendet die Plaetze jenseits der Vorwahl aus.
    s_clay = new Clay(clayConfig(getLang()), clayConfig.custom, { autoHandleEvents: false });
  }
  return s_clay;
}

// Heutiger Tag als KALENDERTAG: Tage vom 01.01.1970 bis zum heutigen Datum
// der Ortszeit. Muss zu plan_today() auf der Uhr passen (src/c/kalender.h) -
// beide zählen ganze Tage, nicht Sekunden, damit die Sommerzeit den Zyklus
// nicht um einen Tag verschiebt. Date.UTC rechnet das Datum ohne Zeitzone.
function todayDay() {
  var now = new Date();
  return Math.floor(Date.UTC(now.getFullYear(), now.getMonth(), now.getDate()) / 86400000);
}

// Der Tag, wie ihn 0.15.0 und frueher zaehlten: Ortsmitternacht durch einen
// Tag. Oestlich von Greenwich der Vortag. Nur noch fuer die Umstellung.
function todayDayBis015() {
  var now = new Date();
  var midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  return Math.floor(midnight.getTime() / 86400000);
}

// Name als UTF-8, auf NAME_BYTES aufgefüllt und notfalls abgeschnitten. Wird
// mitten in einem Mehrbyte-Zeichen geschnitten, fallen dessen Reste weg — ein
// halber Umlaut auf der Uhr wäre schlimmer als ein fehlender Buchstabe.
// ZEICHEN AUSSERHALB DER GRUNDEBENE (Emoji) stehen in JavaScript als ZWEI
// Ersatzzeichen. Bis 0.14.0 wurde jedes einzeln mit 3 Byte geschrieben - das
// ist kein UTF-8, und Telefon wie Boulder lasen den Namen danach als Unsinn.
// Jetzt wird das Paar zu einem 4-Byte-Zeichen; ein einzelnes wird U+FFFD.
function nameBytes(text) {
  var out = [];
  var s = String(text || '');
  for (var i = 0; i < s.length && out.length < NAME_BYTES - 1; i++) {
    var c = s.charCodeAt(i);
    var d = i + 1 < s.length ? s.charCodeAt(i + 1) : 0;
    var paar = c >= 0xd800 && c <= 0xdbff && d >= 0xdc00 && d <= 0xdfff;
    var enc;
    if (paar) {
      var cp = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
      enc = [0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3f), 0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f)];
    } else {
      if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
      if (c < 0x80) enc = [c];
      else if (c < 0x800) enc = [0xc0 | (c >> 6), 0x80 | (c & 0x3f)];
      else enc = [0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)];
    }
    if (out.length + enc.length > NAME_BYTES - 1) break;
    out = out.concat(enc);
    if (paar) i++;
  }
  while (out.length < NAME_BYTES) out.push(0);
  return out;
}

// ------------------------------------------------ Der Anker als Datum
//
// DER ANKER UEBERSTEHT DIE KONFIGSEITE. Die Seite kennt nur "Zyklus laeuft
// seit N Wochen". Bis 0.15.0 wurde daraus bei jedem Speichern ein neuer Anker
// (heute - N Wochen) - und der Zyklus rutschte um die Tage seit dem letzten
// vollen Wochenschritt, bis zu sechs, das Raster "alle X Tage" mit ihm (Audit
// W-H1). Jetzt merkt sich diese Seite zu jedem Platz, welchen Anker sie als
// "seit N Wochen" in die Seite gestellt hat - als DATUM. Kommt beim Speichern
// derselbe Name mit derselben Wochenzahl zurueck, bleibt dieser Anker.
// Nur eine geaenderte Wochenzahl oder ein neuer Name rechnet neu.
var ANKER_KEY = 'supcycle_anker';   // je Platz { name, datum: 'JJJJ-MM-TT', seit } oder null

function datumAusTag(tag) { return new Date(tag * 86400000).toISOString().slice(0, 10); }
function tagAusDatum(datum) {
  var ms = Date.parse(datum);
  return isFinite(ms) ? Math.round(ms / 86400000) : null;
}

// Wie viele volle Wochen seit dem Anker, so wie die Seite sie anbietet.
function seitWochen(anker, today) {
  var seit = Math.floor((today - anker) / 7);
  if (seit < 0) seit = 0;
  if (seit > 25) seit = 25;
  return seit;
}

function gemerkteAnker() {
  try {
    var a = JSON.parse(localStorage.getItem(ANKER_KEY) || 'null');
    return (a && a.length) ? a : [];
  } catch (e) { meldeFehler('Anker lesen', e); return []; }
}

// Die Wochenzahlen, die in die Seite kommen, und dazu die Anker merken.
// Rueckgabe: { SINCE1: '3', ... } fuer die Plaetze mit Eintrag.
function ankerInDieSeite(items) {
  var today = todayDay();
  var merken = [];
  var clay = {};
  for (var i = 0; i < SLOTS; i++) {
    var it = items && items[i];
    if (!it || !it.name || typeof it.anchor !== 'number') { merken.push(null); continue; }
    var seit = seitWochen(it.anchor, today);
    merken.push({ name: it.name, datum: datumAusTag(it.anchor), seit: seit });
    clay['SINCE' + (i + 1)] = String(seit);
  }
  try { localStorage.setItem(ANKER_KEY, JSON.stringify(merken)); } catch (e) { meldeFehler('Anker merken', e); }
  return clay;
}

function int32le(v) {
  var n = v | 0;
  return [n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, (n >> 24) & 0xff];
}

function num(dict, key, fallback) {
  if (dict[key] === undefined) return fallback;
  var v = parseInt(dict[key].value, 10);
  return isFinite(v) ? v : fallback;
}

/**
 * Aus der Antwort der Konfigseite einen Plan bauen.
 *
 * Ein Platz zählt nur, wenn er einen Namen UND einen Rhythmus hat. Ein Name
 * ohne Rhythmus ist ein angefangener Eintrag, kein Präparat — und ein
 * Rhythmus ohne Namen wäre auf der Uhr eine leere Zeile.
 */
function buildPlan(dict) {
  var today = todayDay();
  var gemerkt = gemerkteAnker();
  var bytes = [];
  var items = [];
  var used = 0;

  // Die Vorwahl begrenzt, was zaehlt. Ein Platz jenseits davon ist in der
  // Seite verborgen; seinen alten Inhalt trotzdem zu uebernehmen hiesse,
  // etwas einzuplanen, das niemand mehr sieht.
  var count = num(dict, 'COUNT', SLOTS);
  if (count < 1 || count > SLOTS) count = SLOTS;

  for (var i = 1; i <= SLOTS; i++) {
    var name = i <= count && dict['NAME' + i] !== undefined
      ? String(dict['NAME' + i].value || '').trim() : '';
    if (!name) {
      // Leerer Platz: trotzdem 26 Byte, damit die Reihenfolge stimmt. Die Uhr
      // erkennt ihn am Nullbyte. Ein Platz OHNE NAMEN ist der leere Platz -
      // das ist die einzige Frage, die der Benutzer dafuer beantworten muss.
      bytes = bytes.concat(nameBytes(''), [0, 0, SLOT_UNUSED, 0, 0, 0], int32le(0));
      items.push(null);
      continue;
    }
    used++;

    var minutes = num(dict, 'TIME' + i, 480);
    if (minutes < 0 || minutes > 23 * 60 + 59) minutes = 480;

    // Alle X Tage. Leer oder Unsinn heisst taeglich.
    var every = num(dict, 'EVERY' + i, 1);
    if (!(every >= 1)) every = 1;
    if (every > 30) every = 30;

    // Wochen Einnahme: LEER heisst unbegrenzt, und unbegrenzt heisst 0.
    // Deshalb hier kein Standardwert - ein voreingestelltes '8' machte aus
    // jedem Praeparat ungefragt eine Kur.
    var on = num(dict, 'ON' + i, 0);
    if (!(on >= 1)) on = 0;
    if (on > 52) on = 52;

    // Pause nur, wenn es ueberhaupt einen Zyklus gibt. Ohne Einnahmewochen
    // waere eine Pause eine Angabe ueber etwas, das nicht stattfindet.
    var off = on === 0 ? 0 : num(dict, 'OFF' + i, 0);
    if (!(off >= 1)) off = 0;
    if (off > 52) off = 52;

    // Anker: der Tag, an dem Woche 1 begann. "Zyklus läuft seit N Wochen"
    // schiebt ihn entsprechend zurück, damit die Uhr sofort die richtige
    // Phase zeigt statt bei eins anzufangen - aber nur, wenn die Wochenzahl
    // oder der Name neu ist. Sonst gilt der Anker, den die Seite zeigte.
    var since = num(dict, 'SINCE' + i, 0);
    if (since < 0 || since > 25) since = 0;
    var alt = gemerkt[i - 1];
    var altTag = alt && alt.name === name && alt.seit === since ? tagAusDatum(alt.datum) : null;
    var anchor = altTag !== null ? altTag : today - since * 7;

    bytes = bytes.concat(
      nameBytes(name),
      [minutes / 60 | 0, minutes % 60, SLOT_USED, every, on, off],
      int32le(anchor)
    );
    items.push({
      name: name, hour: minutes / 60 | 0, minute: minutes % 60,
      every: every, on: on, off: off, anchor: anchor
    });
  }

  if (bytes.length !== SLOTS * ITEM_BYTES) {
    console.log('Plan hat ' + bytes.length + ' statt ' + (SLOTS * ITEM_BYTES) + ' Byte');
  }
  return { bytes: bytes, used: used, items: items };
}

// Der Schalter aus der Antwort der Konfigseite. Ein Umschalter kommt je nach
// Fassung als true/false oder als 1/0 zurueck - beides gilt, und fehlt er,
// bleibt die Animation an.
function readFx(dict) {
  var t = dict && dict.FX;
  if (!t || t.value === undefined || t.value === null || t.value === '') return true;
  var v = t.value;
  if (v === false || v === 0 || v === '0' || v === 'false') return false;
  return true;
}

function storedFx() {
  try { return localStorage.getItem(FX_KEY) !== '0'; } catch (e) { meldeFehler('Animation lesen', e); return true; }
}

// ------------------------------------------------ Der Stand der Uhr

// DIE UHR IST DIE EINE STELLE, AN DER DER PLAN GILT. Geaendert wird er hier
// auf der Konfigseite (bis zu seiner Archivierung am 29.09.2026 auch in
// Kiesel-Helper); sie schickt an die Uhr, und die Uhr meldet mit jeder
// Nachricht ihren Plan. Boulder liest ihn nur mit. Diese Seite uebernimmt ihn -
// in die Konfigseite und in die Pins. Bis 0.11 schickte sie auf jede Anfrage
// der Uhr ihren eigenen gespeicherten Plan zurueck und haette einen aus
// Kiesel-Helper damit ueberschrieben.
//
// NUR WAS NICHT ANKAM, GEHT NOCH EINMAL: ein Vermerk steht, solange eine
// Aenderung von der Konfigseite nicht bestaetigt ist.
var PENDING_KEY = 'supcycle_pending';

function pending() {
  try { return localStorage.getItem(PENDING_KEY) === '1'; } catch (e) { meldeFehler('Vermerk lesen', e); return false; }
}
function setPending(on) {
  try { if (on) localStorage.setItem(PENDING_KEY, '1'); else localStorage.removeItem(PENDING_KEY); } catch (e) { meldeFehler('Vermerk schreiben', e); }
}

function mergeClaySettings(values) {
  try {
    var s = JSON.parse(localStorage.getItem('clay-settings') || '{}') || {};
    for (var k in values) { if (values.hasOwnProperty(k)) s[k] = values[k]; }
    localStorage.setItem('clay-settings', JSON.stringify(s));
  } catch (e) { meldeFehler('Konfigseite nachfuehren', e); }
}

// Den Namen aus 16 Byte UTF-8 zurueck in Text.
function nameFromBytes(b) {
  var raw = '';
  for (var i = 0; i < b.length && b[i] !== 0; i++) raw += String.fromCharCode(b[i]);
  try { return decodeURIComponent(escape(raw)); } catch (e) { meldeFehler('Name ist kein UTF-8', e); return raw; }
}

// Den Datenblock der Uhr in Eintraege zerlegen - dasselbe Format, das
// buildPlan baut. Ein leerer Platz ist null.
function itemsFromBytes(bytes) {
  var items = [];
  for (var i = 0; i < SLOTS; i++) {
    var o = i * ITEM_BYTES;
    if (o + ITEM_BYTES > bytes.length || !bytes[o + 18]) { items.push(null); continue; }
    var name = nameFromBytes(bytes.slice(o, o + NAME_BYTES));
    if (!name) { items.push(null); continue; }
    var anchor = (bytes[o + 22] | (bytes[o + 23] << 8) | (bytes[o + 24] << 16) | (bytes[o + 25] << 24));
    items.push({
      name: name, hour: bytes[o + 16], minute: bytes[o + 17],
      every: bytes[o + 19] || 1, on: bytes[o + 20], off: bytes[o + 21], anchor: anchor
    });
  }
  return items;
}

// Den Plan der Uhr uebernehmen: in den Speicher, in die Konfigseite.
function adoptWatchPlan(bytes, fx) {
  var items = itemsFromBytes(bytes);
  var clay = ankerInDieSeite(items);
  var last = 0;
  for (var i = 0; i < SLOTS; i++) {
    var it = items[i];
    var n = i + 1;
    if (!it) {
      clay['NAME' + n] = '';
      continue;
    }
    last = n;
    clay['NAME' + n] = it.name;
    clay['TIME' + n] = String(it.hour * 60 + it.minute);
    clay['EVERY' + n] = String(it.every);
    clay['ON' + n] = it.on ? String(it.on) : '';
    clay['OFF' + n] = it.off ? String(it.off) : '';
  }
  clay.COUNT = String(Math.max(1, last));
  if (fx !== undefined) clay.FX = !!fx;
  try {
    localStorage.setItem(PLAN_KEY, JSON.stringify(Array.prototype.slice.call(bytes)));
    localStorage.setItem(ITEMS_KEY, JSON.stringify(items));
    if (fx !== undefined) localStorage.setItem(FX_KEY, fx ? '1' : '0');
  } catch (e) { meldeFehler('Plan der Uhr speichern', e); }
  mergeClaySettings(clay);
}

function watchPlanEmpty(bytes) {
  for (var i = 0; i < SLOTS; i++) {
    if (bytes[i * ITEM_BYTES + 18]) return false;
  }
  return true;
}

// Die Uhr hat KEINEN Plan, hier steht aber einer: die Uhr ist neu oder wurde
// zurueckgesetzt. Ihr leerer Plan ist dann keine Aenderung, sondern eine
// Luecke, und er darf den gespeicherten NIE ueberschreiben - ob mit oder ohne
// Anfrage. Bis 0.14.0 tat er das, wenn die Startanfrage verloren ging (pkjs
// hoerte noch nicht zu) und die Uhr ihren Stand ohne REQUEST nachholte: der
// Plan auf dem Telefon war danach leer (im Emulator nachgestellt, 03.10.2026).
// Leeren laesst sich der Plan nur auf der Konfigseite, und dann ist auch der
// gespeicherte leer. (Boulder liest den Plan nur. Das archivierte
// Kiesel-Helper konnte ihn auf der Uhr leeren - das holt diese Seite jetzt
// zurueck; ein verlorener Plan ist schlimmer als ein zurueckgekehrter.)
function watchWithoutPlan(watchPlan, stored) {
  return !!(watchPlan && stored && watchPlanEmpty(watchPlan) && !watchPlanEmpty(stored));
}

function sendPlan(bytes, why, fx) {
  // Die Einstellung reist mit dem Plan. Eine eigene Nachricht dafuer waere
  // eine zweite Gelegenheit, unterwegs verloren zu gehen - und der Postausgang
  // fasst ohnehin nur eine auf einmal.
  var msg = { PLAN: bytes };
  if (fx !== undefined) msg.FX = fx ? 1 : 0;
  Pebble.sendAppMessage(msg,
    function () { setPending(false); console.log('Plan geschickt (' + why + ')'); },
    function () { console.log('Plan nicht zugestellt (' + why + ') - Uhr laeuft wohl nicht'); });
}

// Der Plan als Werte, fuer die Pins. Die Bytefolge allein reichte nicht:
// daraus die Namen zurueckzulesen waere Decodierarbeit fuer nichts.
function storedItems() {
  try {
    var raw = localStorage.getItem(ITEMS_KEY);
    if (!raw) return null;
    var arr = JSON.parse(raw);
    return (arr && arr.length) ? arr : null;
  } catch (e) {
    meldeFehler('Eintraege lesen', e);
    return null;
  }
}

// GESPEICHERTE ANKER AUF KALENDERTAGE UMSTELLEN, einmal - wie plan_init auf
// der Uhr, mit derselben Verschiebung (beide Zaehlungen heute). Ohne das
// ginge ein noch nicht zugestellter Plan (Vermerk) oder der Plan fuer eine
// frische Uhr mit Ankern der alten Zaehlung hinaus, und der Zyklus stuende
// einen Tag daneben.
var TAGE_KEY = 'supcycle_tage';   // '2' = Kalendertage
function tageUmstellen() {
  try {
    if (localStorage.getItem(TAGE_KEY) === '2') return;
    var versatz = todayDay() - todayDayBis015();
    if (versatz) {
      var raw = localStorage.getItem(PLAN_KEY);
      var bytes = raw ? JSON.parse(raw) : null;
      if (bytes && bytes.length === SLOTS * ITEM_BYTES) {
        for (var i = 0; i < SLOTS; i++) {
          var o = i * ITEM_BYTES;
          if (!bytes[o + 18]) continue;
          var a = int32le((bytes[o + 22] | (bytes[o + 23] << 8) | (bytes[o + 24] << 16) | (bytes[o + 25] << 24)) + versatz);
          for (var b = 0; b < 4; b++) bytes[o + 22 + b] = a[b];
        }
        localStorage.setItem(PLAN_KEY, JSON.stringify(bytes));
      }
      var rawItems = localStorage.getItem(ITEMS_KEY);
      var items = rawItems ? JSON.parse(rawItems) : null;
      if (items && items.length) {
        for (var k = 0; k < items.length; k++) {
          if (items[k] && typeof items[k].anchor === 'number') items[k].anchor += versatz;
        }
        localStorage.setItem(ITEMS_KEY, JSON.stringify(items));
      }
    }
    localStorage.setItem(TAGE_KEY, '2');
    console.log('Anker als Kalendertage, um ' + versatz + ' verschoben');
  } catch (e) { meldeFehler('Tage umstellen', e); }
}

function storedPlan() {
  try {
    var raw = localStorage.getItem(PLAN_KEY);
    if (!raw) return null;
    var arr = JSON.parse(raw);
    return (arr && arr.length === SLOTS * ITEM_BYTES) ? arr : null;
  } catch (e) {
    meldeFehler('Plan lesen', e);
    return null;
  }
}

// ------------------------------------------------------------------ Timeline

function loadPins() {
  try { return JSON.parse(localStorage.getItem(PIN_STORE)) || {}; } catch (e) { meldeFehler('Pins lesen', e); return {}; }
}
function savePins(o) {
  try { localStorage.setItem(PIN_STORE, JSON.stringify(o)); } catch (e) { meldeFehler('Pins speichern', e); }
}

function pad(n) { return (n < 10 ? '0' : '') + n; }

// Die Uhr schickt das Datum als JJJJMMTT. Frueher kam eine Tagesnummer
// (Tage seit der Epoche) - daraus laesst sich der Kalendertag nicht sicher
// zurueckrechnen, weil die Uhr sie aus ihrer ORTSZEIT bildet. Bei positiver
// Zeitzone landete der Pin einen Tag zu frueh, also in der Vergangenheit.
function dayParts(ymd) {
  return {
    y: Math.floor(ymd / 10000),
    m: Math.floor((ymd % 10000) / 100) - 1,   // 0-basiert wie in Date
    d: ymd % 100
  };
}

function dayKeyOf(ymd) {
  var t = dayParts(ymd);
  return '' + t.y + pad(t.m + 1) + pad(t.d);
}

function buildPin(id, item, ymd, taken, texts) {
  var t = dayParts(ymd);
  var when = new Date(t.y, t.m, t.d, item.hour, item.minute, 0);
  return {
    id: id,
    time: when.toISOString(),
    layout: {
      type: 'genericPin',
      title: item.name,
      subtitle: taken ? texts.taken : 'SupCycle',
      tinyIcon: taken ? ICON_TAKEN : ICON_DUE,
      backgroundColor: PIN_COLOR,
      foregroundColor: '#FFFFFF'
    },
    actions: [{ title: texts.open, type: 'openWatchApp', launchCode: 2 }]
  };
}

function insertViaRest(pin, token, cb) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () { cb(this.status >= 200 && this.status < 300, 'REST ' + this.status); };
  xhr.onerror = function () { cb(false, 'REST Netzwerkfehler'); };
  xhr.open('PUT', API_URL + pin.id);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.setRequestHeader('X-User-Token', '' + token);
  xhr.send(JSON.stringify(pin));
}

function insertViaLocal(pin, cb) {
  try {
    if (Pebble.insertTimelinePin.length >= 3) {
      var done = false;
      var finish = function (ok) { if (!done) { done = true; cb(ok, 'lokal'); } };
      setTimeout(function () { finish(false); }, 5000);
      Pebble.insertTimelinePin(pin, function () { finish(true); }, function () { finish(false); });
    } else {
      Pebble.insertTimelinePin(pin);
      cb(true, 'lokal');
    }
  } catch (e) {
    meldeFehler('Pin lokal', e);
    cb(false, 'lokal: ' + e);
  }
}

function sendAll(queue, insert) {
  (function next() {
    var item = queue.shift();
    if (!item) { console.log('timeline: fertig'); return; }
    insert(item.pin, function (ok, info) {
      console.log('timeline: ' + item.pin.id + ' -> ' + (ok ? 'ok' : 'fehlgeschlagen') +
                  ' (' + info + ')');
      // Pro Pin sichern: pkjs wird mit der App beendet, ein Sammelspeichern am
      // Ende ginge dabei verloren.
      if (ok) {
        var st = loadPins();
        st[item.pin.id] = { sig: item.sig, sentAt: Date.now() };
        savePins(st);
      }
      next();
    });
  })();
}

/**
 * Pins fuer heute setzen.
 *
 * Was heute ansteht, sagt die UHR (Bitmasken) - die Zyklusrechnung bleibt
 * damit an einer einzigen Stelle, in cycle.c. Sie hier in JavaScript
 * nachzubauen hiesse, zwei Wahrheiten zu pflegen, die auseinanderlaufen
 * koennen. Namen und Uhrzeiten kommen aus dem Plan, den diese Seite selbst
 * gebaut hat.
 */
function pushPins(ymd, dueMask, takenMask) {
  var plan = storedItems();
  if (!plan) { console.log('timeline: kein Plan - keine Pins'); return; }

  var texts = PIN_TEXT[getLang()] || PIN_TEXT[0];
  var store = loadPins();
  var now = Date.now();
  Object.keys(store).forEach(function (id) {
    if (store[id] && store[id].sentAt && now - store[id].sentAt > FORGET_AFTER_MS) delete store[id];
  });
  savePins(store);

  var queue = [], wanted = 0;
  for (var i = 0; i < plan.length; i++) {
    if (!(dueMask & (1 << i))) continue;
    var item = plan[i];
    if (!item || !item.name) continue;
    wanted++;
    var taken = (takenMask & (1 << i)) !== 0;
    var id = 'supcycle-' + dayKeyOf(ymd) + '-' + i;
    // Der Zustand steckt in der Signatur: nur was sich geaendert hat, geht
    // erneut hinaus. Ein Pin, der bei jeder Auffrischung neu gesendet wird,
    // meldet jedes Mal eine Aenderung, die keine ist.
    var sig = (taken ? 't' : 'd') + ':' + item.hour + ':' + item.minute + ':' +
              item.name + ':v' + LOOK_VERSION + ':l' + getLang();
    var had = store[id];
    if (had && had.sig === sig && now - had.sentAt < RESEND_AFTER_MS) continue;
    queue.push({ pin: buildPin(id, item, ymd, taken, texts), sig: sig });
  }
  console.log('timeline: ' + queue.length + ' von ' + wanted + ' Pins zu senden');
  if (queue.length === 0) return;

  var useLocal = function (reason) {
    if (typeof Pebble.insertTimelinePin === 'function') {
      console.log('timeline: ' + reason + ', nutze lokale API');
      sendAll(queue, insertViaLocal);
    } else {
      console.log('timeline: ' + reason + ', keine lokale API - Pins uebersprungen');
    }
  };
  if (typeof Pebble.getTimelineToken !== 'function') { useLocal('kein getTimelineToken'); return; }
  Pebble.getTimelineToken(function (token) {
    sendAll(queue, function (pin, cb) { insertViaRest(pin, token, cb); });
  }, function (error) {
    useLocal('kein Token (' + error + ')');
  });
}

// ---------------------------------------------------------------- Ereignisse

Pebble.addEventListener('showConfiguration', function () {
  tageUmstellen();
  // Die Wochenzahlen von heute: seit dem letzten Stand der Uhr koennen Tage
  // vergangen sein, und eine veraltete Zahl saehe beim Speichern aus wie eine
  // geaenderte.
  var items = storedItems();
  if (items) mergeClaySettings(ankerInDieSeite(items));
  Pebble.openURL(getClay().generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  tageUmstellen();
  if (!e || !e.response) return;
  // false = Clay soll nichts von sich aus schicken; aus den Feldern wird erst
  // ein Block gebaut, und der geht als eines hinaus.
  var dict = getClay().getSettings(e.response, false);
  var plan = buildPlan(dict);
  var fx = readFx(dict);
  try {
    localStorage.setItem(PLAN_KEY, JSON.stringify(plan.bytes));
    localStorage.setItem(ITEMS_KEY, JSON.stringify(plan.items));
    localStorage.setItem(FX_KEY, fx ? '1' : '0');
  } catch (err) { meldeFehler('Plan speichern', err); }
  console.log('Plan gespeichert: ' + plan.used + ' Praeparate, Animation ' +
              (fx ? 'an' : 'aus'));
  // Unterwegs, bis die Uhr bestaetigt: kein Stand der Uhr ueberschreibt ihn.
  setPending(true);
  sendPlan(plan.bytes, 'nach dem Speichern', fx);
});

Pebble.addEventListener('appmessage', function (e) {
  tageUmstellen();
  var p = e.payload;
  // Die Uhr sagt beim Start, in welcher Sprache sie beschriftet ist, und
  // fragt zugleich nach dem Plan.
  if (p.LANG !== undefined) {
    try { localStorage.setItem(LANG_KEY, String(knownLang(parseInt(p.LANG, 10)))); } catch (err) { meldeFehler('Sprache speichern', err); }
  }
  var watchPlan = (p.PLAN !== undefined && p.PLAN.length) ? p.PLAN : null;
  var stored = storedPlan();
  if (p.REQUEST !== undefined) {
    if (stored && pending()) {
      // Auf der Konfigseite geaendert, nie angekommen: jetzt.
      sendPlan(stored, 'nachgereicht', storedFx());
    } else if (watchWithoutPlan(watchPlan, stored)) {
      // Die Uhr hat keinen Plan (neu installiert), hier steht einer.
      sendPlan(stored, 'Uhr ohne Plan', storedFx());
    } else if (watchPlan) {
      adoptWatchPlan(watchPlan, p.FX);
    } else if (stored) {
      // Eine Uhr ohne Planmeldung (aeltere Fassung): wie frueher.
      sendPlan(stored, 'auf Anfrage', storedFx());
    }
  } else if (watchPlan && !pending()) {
    if (watchWithoutPlan(watchPlan, stored)) {
      // Dieselbe Luecke, nur kam die Anfrage nie an: nicht uebernehmen,
      // sondern der Uhr den Plan geben.
      sendPlan(stored, 'Uhr ohne Plan, ohne Anfrage', storedFx());
    } else {
      adoptWatchPlan(watchPlan, p.FX);
    }
  }
  // Die Uhr sagt, was heute ansteht und was davon schon genommen ist.
  if (p.TODAY !== undefined && p.DUE !== undefined) {
    pushPins(p.TODAY, p.DUE, p.TAKEN || 0);
  }
  // DIE UHR FRAGT NACH DER ZEIT, wenn sie hinter ihrem gemerkten Tag steht:
  // ob sie jetzt falsch geht (Neustart) oder vorher falsch ging, weiss nur das
  // Telefon (src/c/plan.c). Die Antwort traegt nur die Zeit; entscheiden tut
  // die Uhr.
  if (p.UHRZEIT !== undefined) {
    var jetzt = Math.floor(Date.now() / 1000);
    Pebble.sendAppMessage({ UHRZEIT: jetzt },
      function () { console.log('Zeit an die Uhr: ' + jetzt + ' (Uhr: ' + p.UHRZEIT + ')'); },
      function () { console.log('Zeit nicht zugestellt'); });
  }
});

Pebble.addEventListener('ready', function () {
  tageUmstellen();
  console.log('SupCycle bereit');
});
