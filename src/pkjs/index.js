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
var ITEM_BYTES = 25;        // muss zu SC_ITEM_BYTES passen
var PLAN_KEY = 'supcycle_plan';
var LANG_KEY = 'supcycle_lang';

var MODE_UNUSED = 0, MODE_DAILY = 1, MODE_CYCLIC = 2;

function getLang() {
  var v = parseInt(localStorage.getItem(LANG_KEY), 10);
  return v === 1 ? 1 : 0;
}

// Clay erst bauen, wenn die Seite gebraucht wird: dann steht die Sprache der
// Uhr schon fest. Eine gebaute Instanz bleibt, damit showConfiguration und
// webviewclosed dieselbe benutzen.
var s_clay = null;
function getClay() {
  if (!s_clay) s_clay = new Clay(clayConfig(getLang()), null, { autoHandleEvents: false });
  return s_clay;
}

// Heutiger Tag als Tage seit der Epoche, aus der ORTSZEIT gerechnet. Muss zu
// plan_today() auf der Uhr passen — beide zählen ganze Tage, nicht Sekunden,
// damit die Sommerzeit den Zyklus nicht um einen Tag verschiebt.
function todayDay() {
  var now = new Date();
  var midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  return Math.floor(midnight.getTime() / 86400000);
}

// Name als UTF-8, auf NAME_BYTES aufgefüllt und notfalls abgeschnitten. Wird
// mitten in einem Mehrbyte-Zeichen geschnitten, fallen dessen Reste weg — ein
// halber Umlaut auf der Uhr wäre schlimmer als ein fehlender Buchstabe.
function nameBytes(text) {
  var out = [];
  var s = String(text || '');
  for (var i = 0; i < s.length && out.length < NAME_BYTES - 1; i++) {
    var c = s.charCodeAt(i);
    var enc;
    if (c < 0x80) enc = [c];
    else if (c < 0x800) enc = [0xc0 | (c >> 6), 0x80 | (c & 0x3f)];
    else enc = [0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)];
    if (out.length + enc.length > NAME_BYTES - 1) break;
    out = out.concat(enc);
  }
  while (out.length < NAME_BYTES) out.push(0);
  return out;
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
  var bytes = [];
  var used = 0;

  for (var i = 1; i <= SLOTS; i++) {
    var name = dict['NAME' + i] !== undefined ? String(dict['NAME' + i].value || '').trim() : '';
    var mode = num(dict, 'MODE' + i, MODE_UNUSED);
    if (!name || mode === MODE_UNUSED) {
      // Leerer Platz: trotzdem 25 Byte, damit die Reihenfolge stimmt. Die Uhr
      // erkennt ihn am Modus 0.
      bytes = bytes.concat(nameBytes(''), [0, 0, MODE_UNUSED, 0, 0], int32le(0));
      continue;
    }
    used++;

    var minutes = num(dict, 'TIME' + i, 480);
    if (minutes < 0 || minutes > 23 * 60 + 59) minutes = 480;

    var on = num(dict, 'ON' + i, 8);
    var off = num(dict, 'OFF' + i, 2);
    if (on < 1) on = 1;
    if (on > 26) on = 26;
    if (off < 1) off = 1;
    if (off > 12) off = 12;

    // Bei einem täglichen Präparat gibt es keine Pause. Die Uhr rechnet das
    // zwar auch so, aber ein weeks_off im Block wäre eine Behauptung, die
    // nirgends gilt.
    if (mode === MODE_DAILY) off = 0;

    // Anker: der Tag, an dem Woche 1 begann. "Zyklus läuft seit N Wochen"
    // schiebt ihn entsprechend zurück, damit die Uhr sofort die richtige
    // Phase zeigt statt bei eins anzufangen.
    var since = num(dict, 'SINCE' + i, 0);
    if (since < 0 || since > 25) since = 0;
    var anchor = today - since * 7;

    bytes = bytes.concat(
      nameBytes(name),
      [minutes / 60 | 0, minutes % 60, mode, on, off],
      int32le(anchor)
    );
  }

  if (bytes.length !== SLOTS * ITEM_BYTES) {
    console.log('Plan hat ' + bytes.length + ' statt ' + (SLOTS * ITEM_BYTES) + ' Byte');
  }
  return { bytes: bytes, used: used };
}

function sendPlan(bytes, why) {
  Pebble.sendAppMessage({ PLAN: bytes },
    function () { console.log('Plan geschickt (' + why + ')'); },
    function () { console.log('Plan nicht zugestellt (' + why + ') - Uhr laeuft wohl nicht'); });
}

function storedPlan() {
  try {
    var raw = localStorage.getItem(PLAN_KEY);
    if (!raw) return null;
    var arr = JSON.parse(raw);
    return (arr && arr.length === SLOTS * ITEM_BYTES) ? arr : null;
  } catch (e) {
    return null;
  }
}

// ---------------------------------------------------------------- Ereignisse

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(getClay().generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  // false = Clay soll nichts von sich aus schicken; aus den Feldern wird erst
  // ein Block gebaut, und der geht als eines hinaus.
  var dict = getClay().getSettings(e.response, false);
  var plan = buildPlan(dict);
  try { localStorage.setItem(PLAN_KEY, JSON.stringify(plan.bytes)); } catch (err) {}
  console.log('Plan gespeichert: ' + plan.used + ' Praeparate');
  sendPlan(plan.bytes, 'nach dem Speichern');
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload;
  // Die Uhr sagt beim Start, in welcher Sprache sie beschriftet ist, und
  // fragt zugleich nach dem Plan.
  if (p.LANG !== undefined) {
    try { localStorage.setItem(LANG_KEY, String(p.LANG === 1 ? 1 : 0)); } catch (err) {}
  }
  if (p.REQUEST !== undefined) {
    var stored = storedPlan();
    if (stored) sendPlan(stored, 'auf Anfrage');
    else console.log('Kein Plan gespeichert - nichts zu schicken');
  }
});

Pebble.addEventListener('ready', function () {
  console.log('SupCycle bereit');
});
