#!/bin/sh
# Hakenzeiten, Zuruecknehmen und den Tageswechsel (plan.c) mit dem
# C-Compiler des Rechners pruefen - pebble.h kommt als Attrappe aus tools/host.
#
#   sh tools/plan_host_test.sh
#
# Exitcode 0 = alles bestanden.
DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
cc -std=c99 -Wall -Wextra -I "$DIR/tools/host" -I "$DIR/src/c" \
   "$DIR/tools/plan_host_test.c" "$DIR/src/c/plan.c" "$DIR/src/c/cycle.c" "$DIR/src/c/prefs.c" \
   "$DIR/tools/host/attrappe.c" "$DIR/tools/host/attrappe_persist.c" \
   -o "$OUT/plan_test" || exit 1
TZ=UTC "$OUT/plan_test"
