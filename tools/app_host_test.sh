#!/bin/sh
# Die ganze App (src/c, mit supcycle.c und allen Fenstern) mit dem
# C-Compiler des Rechners pruefen - Fenster, Tasten, Wecker und Telefon kommen
# als Attrappe aus tools/host.
#
#   sh tools/app_host_test.sh
#
# Die Nachrichtenschluessel entstehen wie in phone_host_test.sh aus
# package.json. main() aus supcycle.c heisst hier supcycle_main. Laeuft in
# Zuerich, London und New York - Runden ueber Mitternacht haengen am Datum
# der Ortszeit.
#
# Exitcode 0 = in jeder Zone alles bestanden.
DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
node -e "require(\"$DIR/package.json\").pebble.messageKeys.forEach((k, i) =>
  console.log(\"#define MESSAGE_KEY_\" + k + \" \" + (10000 + i)))" > "$OUT/message_keys.auto.h" || exit 1
FLAGS="-std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -I $DIR/tools/host -I $DIR/src/c -include $OUT/message_keys.auto.h"
cc $FLAGS -Dmain=supcycle_main -c "$DIR/src/c/supcycle.c" -o "$OUT/supcycle.o" || exit 1
QUELLEN=""
for f in "$DIR"/src/c/*.c; do
  [ "$(basename "$f")" = supcycle.c ] || QUELLEN="$QUELLEN $f"
done
# shellcheck disable=SC2086
cc $FLAGS "$DIR/tools/app_host_test.c" "$OUT/supcycle.o" $QUELLEN \
   "$DIR/tools/host/attrappe.c" "$DIR/tools/host/attrappe_persist.c" "$DIR/tools/host/attrappe_ui.c" \
   -o "$OUT/app_test" || exit 1
FEHLER=0
for ZONE in Europe/Zurich Europe/London America/New_York; do
  TZ=$ZONE "$OUT/app_test" || FEHLER=1
done
exit $FEHLER
