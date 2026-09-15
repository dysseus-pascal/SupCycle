#!/bin/sh
# Die Zyklusrechnung in cycle.c pruefen - auf der Zielarchitektur.
#
#   sh tools/selftest.sh <Quellordner>
#
# Auf dem Baurechner steht kein C-Compiler (kein gcc, kein sudo fuer apt), nur
# der ARM-Compiler der SDK. Ein Test, der nie laeuft, ist keiner - also laeuft
# er dort, wo ohnehin uebersetzt wird: im Emulator. Das prueft nebenbei mehr
# als ein Lauf auf x86-64, weil Typbreiten und Ausrichtung dann nachweislich
# auf 32-Bit-ARM stimmen.
#
# Das Log liegt unter $HOME, NICHT unter /tmp: /tmp wird zwischen WSL-Sitzungen
# geleert, und dann waere hinterher nichts mehr nachzulesen.
#
# Exitcode 0 = alle Pruefungen bestanden.
export PATH=$HOME/.local/bin:$PATH
SRC="${1:-$SUPCYCLE_SRC}"
E="--emulator emery"
LOG=$HOME/supcycle-selftest.log

sh "$SRC/tools/sync_supcycle.sh" "$SRC" selftest || exit 1
cd $HOME/supcycle || exit 1

pebble kill >/dev/null 2>&1
sleep 2
pebble wipe >/dev/null 2>&1
# Erster Lauf startet den Emulator; der braucht Zeit, bevor sich ein Logger
# anhaengen kann.
pebble install $E >/dev/null 2>&1
sleep 6
pebble logs $E > "$LOG" 2>&1 &
LOGPID=$!
sleep 3
# Zweiter Lauf: jetzt schreibt der Selbsttest in ein mitlaufendes Log.
pebble install $E >/dev/null 2>&1
sleep 15
kill $LOGPID 2>/dev/null
pebble kill >/dev/null 2>&1

echo "----- Selbsttest ($(wc -l < "$LOG") Logzeilen) -----"
sed -n '/== 8 Wochen an, 2 aus ==/,/SELBSTTEST FERTIG/p' "$LOG" | sed 's/^\[[^]]*\] [^ ]*> //'
echo "-------------------------------------------"
if grep -q 'SELBSTTEST FERTIG, Fehler: 0' "$LOG"; then
  echo "BESTANDEN"
  exit 0
fi
echo "NICHT BESTANDEN oder Log unvollstaendig - siehe $LOG"
grep 'FEHLER' "$LOG" | head -20
exit 1
