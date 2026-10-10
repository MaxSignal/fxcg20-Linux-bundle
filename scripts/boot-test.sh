#!/bin/bash
# Boot test on the emulator, as on the calculator: the loader add-in
# stages out/linux.bin and starts it, BusyBox comes up, and a command
# typed on the key matrix runs.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

[ -x "$WORK/cgemu" ] || scripts/build-emu.sh
log=$OUT/boot-test.log
"$WORK/cgemu" linux -g3a "$OUT/fxcglinux.g3a" -file "$OUT/linux.bin" \
	-font "$OUT/font6x8.h" -ins 1000000000 \
	-type 'echo $((6*7)) ok; uname -m\n' -png "$OUT/boot-test.png" | tee "$log"

fail() { echo "FAIL: $1" >&2; exit 1; }
grep -q 'Welcome to Linux on the Casio fx-CG20!' "$log" || fail "no welcome message"
grep -q '^42 ok$' "$log" || fail "the typed command did not run"
grep -q '^sh4al-dspeb$' "$log" || fail "uname -m"
echo "PASS (screen: $OUT/boot-test.png)"
