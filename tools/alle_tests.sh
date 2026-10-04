#!/bin/sh
# Alle Pruefungen, die ohne Uhr und ohne Emulator laufen - so faehrt sie
# auch die CI (.github/workflows/bauen.yml), vor dem Bauen.
#
#   sh tools/alle_tests.sh
#
# Die C-Tests (tools/*host_test.sh) waehlen ihre Zeitzonen selbst; die
# Node-Tests laufen hier je in Zuerich, London und New York - die Tage der
# Telefonseite haengen an der Zone des Telefons. pkjs_clay_test.js braucht
# Clay aus node_modules (npm install).
#
# Exitcode 0 = alles bestanden; jeder rote Test macht ihn 1.
DIR=$(cd "$(dirname "$0")/.." && pwd)
FEHLER=0
for t in "$DIR"/tools/*host_test.sh; do
  echo "== $(basename "$t")"
  sh "$t" > "${TMPDIR:-/tmp}/supcycle-test.log" 2>&1 || { cat "${TMPDIR:-/tmp}/supcycle-test.log"; FEHLER=1; }
  tail -1 "${TMPDIR:-/tmp}/supcycle-test.log"
done
for ZONE in Europe/Zurich Europe/London America/New_York; do
  for t in "$DIR"/tools/*test*.js; do
    echo "== $(basename "$t") ($ZONE)"
    TZ=$ZONE node "$t" > "${TMPDIR:-/tmp}/supcycle-test.log" 2>&1 || { cat "${TMPDIR:-/tmp}/supcycle-test.log"; FEHLER=1; }
    tail -1 "${TMPDIR:-/tmp}/supcycle-test.log"
  done
done
echo "== catch_check.js"
node "$DIR/tools/catch_check.js" || FEHLER=1
echo "== strings_check.js"
node "$DIR/tools/strings_check.js" "$DIR/src/c/strings_table.h" "$DIR/src/c" || FEHLER=1
if [ $FEHLER -ne 0 ]; then echo "NICHT BESTANDEN"; else echo "alles bestanden"; fi
exit $FEHLER
