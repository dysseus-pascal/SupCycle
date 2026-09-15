#!/bin/sh
# Den Weckpfad und die Genommen-Animation im Emulator durchspielen.
#
#   sh tools/test_remind.sh <Quellordner> [plattform]
#
# Zwei Durchläufe:
#
#   1. "wake"  Wecker in einer Minute statt zur Planzeit. App verlassen,
#              warten, und nachsehen ob sie sich selbst wieder öffnet, das
#              Erinnerungsfenster zeigt und nach "Genommen" wieder schliesst.
#   2. "slow"  dasselbe mit FX_MS 9000 — die Animation läuft in Zeitlupe,
#              damit `pebble screenshot` sie überhaupt trifft. Gezeichnet wird
#              exakt dasselbe wie im Betrieb, nur langsamer.
#
# Der Wecker ist der Teil, der am ehesten still danebengeht: einer, der nie
# feuert, meldet sich nicht.
export PATH=$HOME/.local/bin:$PATH
SRC="${1:-$SUPCYCLE_SRC}"
P="${2:-emery}"
E="--emulator $P"
OUT=$HOME/sc-remind-$P
LOG=$HOME/sc-remind.log
rm -rf "$OUT"; mkdir -p "$OUT"

shot()  { sleep 1; pebble screenshot $E "$OUT/$1.png" >/dev/null 2>&1; echo "  $(date +%T) $1"; }
click() { pebble emu-button $E click "$1" >/dev/null 2>&1; sleep 1; }

boot() {
  pebble kill >/dev/null 2>&1
  sleep 2
  pebble wipe >/dev/null 2>&1
  pebble install $E >/dev/null 2>&1
  sleep 7
}

echo "== 1. Weckpfad =="
sh "$SRC/tools/sync_supcycle.sh" "$SRC" wake 2>&1 | grep -iE 'error|Build failed' && exit 1
cd $HOME/supcycle || exit 1
boot
pebble logs $E > "$LOG" 2>&1 &
LOGPID=$!
sleep 2
pebble install $E >/dev/null 2>&1
sleep 8
shot 1-heute
echo "-- App verlassen, der Wecker soll sie selbst wieder oeffnen --"
click back
sleep 3
echo "-- warten (rund eine Minute) --"
sleep 58
shot 2-erinnerung
click select
sleep 3
shot 3-nach-genommen
kill $LOGPID 2>/dev/null
pebble kill >/dev/null 2>&1

echo "== 2. Animation in Zeitlupe =="
sh "$SRC/tools/sync_supcycle.sh" "$SRC" slow 2>&1 | grep -iE 'error|Build failed' && exit 1
cd $HOME/supcycle || exit 1
boot
click back
sleep 60
# Ab hier läuft die Erinnerung; "Genommen" drücken und mitfotografieren.
pebble emu-button $E click select >/dev/null 2>&1
i=1
while [ $i -le 10 ]; do
  pebble screenshot $E "$OUT/fx-$(printf %02d $i).png" >/dev/null 2>&1
  i=$((i+1))
done
pebble kill >/dev/null 2>&1

echo "----- Log -----"
grep -iE "Wecker|Vom Wecker|Plan" "$LOG" | sed 's/^\[[^]]*\] [^ ]*> //' | head -8
echo "---------------"

# Die Bilder liegen unter $OUT. Wer sie anderswo braucht, setzt
# SUPCYCLE_SHOTS auf ein Zielverzeichnis - ein fester Pfad hier hinge an
# genau einem Rechner.
if [ -n "$SUPCYCLE_SHOTS" ]; then
  mkdir -p "$SUPCYCLE_SHOTS"; cp "$OUT"/*.png "$SUPCYCLE_SHOTS/" 2>/dev/null
fi
echo "$(ls "$OUT" | wc -l) Bilder in $OUT"

# Sauberen Stand wiederherstellen.
sh "$SRC/tools/sync_supcycle.sh" "$SRC" >/dev/null 2>&1
