#!/bin/sh
# Die Zyklusrechnung in cycle.c pruefen - auf der Zielarchitektur.
#
#   sh tools/selftest.sh <Quellordner>
#
# Er laeuft im Emulator, auf der 32-Bit-ARM-Zielarchitektur, wo die Rechnung
# auch im Betrieb laeuft: Typbreiten und Ausrichtung stimmen dann
# nachweislich dort. (Die Host-Tests plan_host_test.sh und phone_host_test.sh
# laufen dagegen mit dem C-Compiler des Rechners.)
#
# Das Log liegt unter $HOME, NICHT unter /tmp: /tmp kann zwischen Sitzungen
# geleert werden, und dann waere hinterher nichts mehr nachzulesen.
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
