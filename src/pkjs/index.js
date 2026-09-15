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
var ITEMS_KEY = 'supcycle_items';
var LANG_KEY = 'supcycle_lang';

var MODE_UNUSED = 0, MODE_DAILY = 1, MODE_CYCLIC = 2;

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
var LOOK_VERSION = 1;

// Nur Namen aus dem System-Satz erreichen die echte Uhr: die Telefon-App setzt
// das Symbol ueber eine feste Tabelle, die ausschliesslich "system://images/..."
// kennt. Ein unbekannter Name wird stillschweigend weggelassen, und die Uhr
// zeichnet ihre Standardflagge.
var ICON_DUE = 'system://images/NOTIFICATION_REMINDER';
var ICON_TAKEN = 'system://images/GENERIC_CONFIRMATION';

// Spalte 0 ist Englisch, wie in strings_table.h.
var PIN_TEXT = [
  { taken: 'taken', open: 'Open app' },
  { taken: 'genommen', open: 'App oeffnen' }
];

function getLang() {
  var v = parseInt(localStorage.getItem(LANG_KEY), 10);
  return v === 1 ? 1 : 0;
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
    var mode = i <= count ? num(dict, 'MODE' + i, MODE_UNUSED) : MODE_UNUSED;
    if (!name || mode === MODE_UNUSED) {
      // Leerer Platz: trotzdem 25 Byte, damit die Reihenfolge stimmt. Die Uhr
      // erkennt ihn am Modus 0.
      bytes = bytes.concat(nameBytes(''), [0, 0, MODE_UNUSED, 0, 0], int32le(0));
      items.push(null);
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
    items.push({ name: name, hour: minutes / 60 | 0, minute: minutes % 60 });
  }

  if (bytes.length !== SLOTS * ITEM_BYTES) {
    console.log('Plan hat ' + bytes.length + ' statt ' + (SLOTS * ITEM_BYTES) + ' Byte');
  }
  return { bytes: bytes, used: used, items: items };
}

function sendPlan(bytes, why) {
  Pebble.sendAppMessage({ PLAN: bytes },
    function () { console.log('Plan geschickt (' + why + ')'); },
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
    return null;
  }
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

// ------------------------------------------------------------------ Timeline

function loadPins() {
  try { return JSON.parse(localStorage.getItem(PIN_STORE)) || {}; } catch (e) { return {}; }
}
function savePins(o) {
  try { localStorage.setItem(PIN_STORE, JSON.stringify(o)); } catch (e) {}
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
  Pebble.openURL(getClay().generateUrl());
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  // false = Clay soll nichts von sich aus schicken; aus den Feldern wird erst
  // ein Block gebaut, und der geht als eines hinaus.
  var dict = getClay().getSettings(e.response, false);
  var plan = buildPlan(dict);
  try {
    localStorage.setItem(PLAN_KEY, JSON.stringify(plan.bytes));
    localStorage.setItem(ITEMS_KEY, JSON.stringify(plan.items));
  } catch (err) {}
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
  // Die Uhr sagt, was heute ansteht und was davon schon genommen ist.
  if (p.TODAY !== undefined && p.DUE !== undefined) {
    pushPins(p.TODAY, p.DUE, p.TAKEN || 0);
  }
});

Pebble.addEventListener('ready', function () {
  console.log('SupCycle bereit');
});
