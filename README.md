# SupCycle

Präparate im Blick behalten: was heute ansteht, und wo die zyklischen gerade in
ihrem Zyklus stehen.

Die Oberfläche folgt der **Sprache der Uhr** (Deutsch und Englisch, Englisch als
Rückfall) und dem Timeline-Look der Schwesterapps: weisser Grund, schwarze
Schrift, dunkle Seitenleiste rechts. Läuft auf emery, flint und gabbro.

## Bedienung

Ein Bildschirm, zwei Ansichten.

| Taste | Aktion |
|---|---|
| Oben | zwischen **Heute** und **Zyklus** wechseln |
| Mitte | den nächsten offenen Eintrag abhaken |
| Zurück | App verlassen |

**Heute** zeigt, was ansteht, mit Haken bei dem, was schon genommen ist. Was
heute nicht dran ist, steht gar nicht da — in der Pause will man nicht daran
erinnert werden, dass man pausiert. Die Haken gelten für einen Tag und fallen um
Mitternacht weg.

**Zyklus** zeigt für jedes Präparat die laufende Phase: „Woche 3 von 8, noch 40
Tage" oder „Pause, noch 11 Tage". Dauerhafte stehen als „täglich" da.

Der Stand steht auch im App-Glance des Starters.

## Der Plan

Alles wird auf der **Konfigseite der Telefon-App** eingetragen — sechs Plätze,
je mit Name, Uhrzeit, Rhythmus (täglich oder zyklisch), Wochen Einnahme, Wochen
Pause und „Zyklus läuft seit". Ein Platz ohne Namen bleibt leer.

Sechs feste Plätze statt einer Liste zum Anlegen und Löschen: Clay kennt keine
dynamischen Listen, und sie nachzubauen hiesse, die Seite selbst zu schreiben.
Sechs Zeilen decken jeden Stack ab, den man von Hand pflegt.

**„Zyklus läuft seit"** verschiebt den Ankertag zurück, damit die Uhr sofort die
richtige Phase zeigt, statt bei Woche 1 anzufangen. Wer seit fünf Wochen
Ashwagandha nimmt, trägt fünf Wochen ein und sieht Woche 5.

Die Uhr bekommt den fertigen Plan als **einen Datenblock** (25 Byte je Eintrag)
und rechnet daraus selbst aus, was heute ansteht. Sie fragt nie wieder nach und
läuft ohne Telefon mit dem zuletzt empfangenen Plan weiter.

Wie bei Drinktervall liegt der Plan **zusätzlich im Telefonspeicher**: die
Konfigseite geht auch dann auf, wenn die Watchapp nicht läuft, und dann erreicht
sie kein AppMessage. Der Block wird beim nächsten Start nachgereicht. Ohne
diesen zweiten Weg verpuffte jede Eingabe still.

## Gerechnet wird in Tagen

Nicht in Sekunden. Sommerzeit verschiebt einen Tag um eine Stunde, und über acht
Wochen summiert sich das zu einem Fehler von einem Tag — der Zyklus schaltete
dann einen Tag zu früh oder zu spät um. Uhr und Telefon zählen beide ganze Tage
seit der Epoche, aus der Ortszeit.

## Bauen und prüfen

```sh
tools/sync_supcycle.sh <Quellordner>            # spiegeln + bauen
tools/sync_supcycle.sh <Quellordner> demo       # mit Beispielplan
sh tools/selftest.sh <Quellordner>                  # Zyklusrechnung
node tools/strings_check.js src/c/strings_table.h
```

`src/c/cycle.c` hängt bewusst an nichts — kein `pebble.h`, nur Ganzzahlen. Das
ist hier kein Selbstzweck: **eine Zyklusrechnung, die um einen Tag danebenliegt,
fällt im Betrieb erst nach Wochen auf, und dann hat man schon falsch dosiert.**
`cycle_selftest.c` stellt sie gegen 34 von Hand nachgerechnete Erwartungen.

Der Test läuft **im Emulator, auf der 32-Bit-ARM-Zielarchitektur** — auf dem
Baurechner steht kein C-Compiler, nur der ARM-Compiler der SDK. Ein Test, der
nie läuft, ist keiner.

`demo` setzt einen Beispielplan ein, damit sich beide Ansichten im Emulator
ansehen lassen; dort gibt es keine Konfigseite. Die Zyklen stehen dabei
absichtlich in verschiedenen Phasen — eines in der Einnahme, eines in der Pause
—, sonst sähe man nur den halben Fall.

## Noch nicht drin

**Erinnerungen.** Die Uhrzeit je Präparat wird eingetragen, angezeigt und
gespeichert, aber es klingelt noch nichts. Wakeups und Timeline-Pins sind der
nächste Schritt; die Mechanik dafür steht in Drinktervall bereit.

**Health Connect.** Für diesen Stack lohnt es nicht: Kreatin, Maca und
Ashwagandha haben dort keine Felder, und beim Multivitamin fehlen ausgerechnet
B6 und B12. Was eintragbar wäre, kostete mehr Tipparbeit als es einbringt.

## Lizenz

Gemeinfrei, [CC0 1.0](LICENSE). Kopieren, ändern, verkaufen, einbauen — ohne
Bedingung, ohne Namensnennung, ohne Rückfrage.
