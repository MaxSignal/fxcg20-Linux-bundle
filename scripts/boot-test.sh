#!/bin/bash
# Boot test on the emulator, as on the calculator: the loader add-in
# stages out/linux.bin and starts it, BusyBox comes up, and commands
# typed on the key matrix run. Each scenario is a fresh boot.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

[ -x "$WORK/cgemu" ] || scripts/build-emu.sh

fail() { echo "FAIL ($1): $2" >&2; exit 1; }

# run NAME KEYS: boot, type KEYS at the prompt, keep the final screen
run() {
	"$WORK/cgemu" linux -g3a "$OUT/fxcglinux.g3a" -file "$OUT/linux.bin" \
		-font "$OUT/font6x8.h" -ins 1500000000 -type "$2" \
		-png "$OUT/boot-test-$1.png" > "$OUT/boot-test-$1.log"
	cat "$OUT/boot-test-$1.log"
}

# 1. boot, arithmetic in the shell, a program, a pipe and /proc
run shell 'echo $((6*7)) ok; uname -m; ls /proc | wc -l > /tmp/n; cat /tmp/n\n'
log=$OUT/boot-test-shell.log
grep -q 'Welcome to Linux on the Casio fx-CG20!' "$log" || fail shell "no welcome message"
grep -q '^42 ok$' "$log" || fail shell "the typed command did not run"
grep -q '^sh4al-dspeb$' "$log" || fail shell "uname -m"
grep -qE '^[0-9]+$' "$log" || fail shell "pipe into tmpfs"

# 2. full-screen editing: vi, Esc (EXIT key), :wq
run vi 'vi /tmp/x\nihello calculator\e:wq\ncat /tmp/x\n'
grep -q '^hello calculator$' "$OUT/boot-test-vi.log" || fail vi "the file was not written"

echo "PASS (screens: $OUT/boot-test-*.png)"
