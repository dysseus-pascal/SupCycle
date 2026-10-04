#!/bin/sh
# Den Weckplan (remind.c) mit dem C-Compiler des Rechners pruefen - die
# Wecker kommen aus einer Attrappe nach pebbleos (tools/host/attrappe.c), die
# Zeitzonen aus der C-Bibliothek: Zuerich, London, New York.
#
#   sh tools/remind_host_test.sh
#
# Exitcode 0 = in jeder Zone alles bestanden.
DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c99 -Wall -Wextra -I "$DIR/tools/host" -I "$DIR/src/c" \
   "$DIR/tools/remind_host_test.c" "$DIR/src/c/remind.c" "$DIR/src/c/plan.c" "$DIR/src/c/kalender.c" \
   "$DIR/src/c/cycle.c" "$DIR/tools/host/attrappe.c" "$DIR/tools/host/attrappe_persist.c" \
   -o "$OUT/remind_test" || exit 1
FEHLER=0
for ZONE in Europe/Zurich Europe/London America/New_York; do
  TZ=$ZONE "$OUT/remind_test" || FEHLER=1
done
exit $FEHLER
