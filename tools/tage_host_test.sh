#!/bin/sh
# Tage, Zyklen und den gespeicherten Plan (kalender.c, plan.c, cycle.c) mit
# dem C-Compiler des Rechners pruefen - in vier Zeitzonen: Zuerich (Versatz
# +1), London (wechselt zwischen +0 und +1), New York (negativ) und UTC.
#
#   sh tools/tage_host_test.sh
#
# Exitcode 0 = in jeder Zone alles bestanden.
DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
# SC_SELFTEST: cycle_selftest.c, sonst nur im Pruefbau fuer den Emulator.
cc -std=c99 -Wall -Wextra -DSC_SELFTEST -I "$DIR/tools/host" -I "$DIR/src/c" \
   "$DIR/tools/tage_host_test.c" "$DIR/src/c/plan.c" "$DIR/src/c/kalender.c" "$DIR/src/c/cycle.c" \
   "$DIR/src/c/cycle_selftest.c" \
   "$DIR/tools/host/attrappe.c" "$DIR/tools/host/attrappe_persist.c" \
   -o "$OUT/tage_test" || exit 1
FEHLER=0
for ZONE in Europe/Zurich Europe/London America/New_York UTC; do
  TZ=$ZONE "$OUT/tage_test" || FEHLER=1
done
exit $FEHLER
