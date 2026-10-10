#!/bin/bash
# Boot test on the emulator, as on the calculator: the loader add-in
# stages out/linux.bin and starts it, BusyBox comes up, and commands
# typed on the key matrix run. Each scenario is a fresh boot.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

[ -x "$WORK/cgemu" ] || scripts/build-emu.sh

fail() { echo "FAIL ($1): $2" >&2; exit 1; }

# run NAME KEYS [EMULATOR ARGS]: boot through the loader, type KEYS at the
# prompt, keep the final screen
run() {
	"$WORK/cgemu" linux -g3a "$OUT/fxcglinux.g3a" -file "$OUT/linux.bin" \
		-font "$OUT/font6x8.h" -ins 4000000000 -type "$2" "${@:3}" \
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

# 3. the kernel that runs in place from the flash: with xip.bin in the
# storage memory and nothing in the free flash, EXE (as F2) boots linux.bin
# to install it, which then starts it. Only the free flash below 0xb40000
# may change, and it must then hold xip.bin exactly.
python3 scripts/fls.py mkimage "$OUT/xip-before.bin" xip.bin="$OUT/xip.bin" > /dev/null
xip=(-fls xip.bin="$OUT/xip.bin")
run xip-install 'mount\n' "${xip[@]}" -flash "$OUT/xip-before.bin" -flash-out "$OUT/xip-after.bin"
grep -q '^mtd0 on / type romfs' "$OUT/boot-test-xip-install.log" || fail xip-install "it did not start the new kernel"
check_flash() {	# BEFORE AFTER XIP|-: compare, skipping the loader's page at 0x300000
	python3 - "$@" <<'PY'
import sys
before, after = (open(f, "rb").read() for f in sys.argv[1:3])
end = 0xB40000
if sys.argv[3] == "-":
    start, xip = end, b""
else:
    xip = open(sys.argv[3], "rb").read()
    start = end - len(xip)
assert after[start:end] == xip, "the image in the flash differs"
for lo, hi in ((0, 0x300000), (0x310000, start), (end, len(after))):
    assert after[lo:hi] == before[lo:hi], f"the flash changed in {lo:#x}-{hi:#x}"
PY
}
check_flash "$OUT/xip-before.bin" "$OUT/xip-after.bin" "$OUT/xip.bin" || fail xip-install "the flash"

# 4. from then on the loader boots it: the kernel and the programs run from
# the flash, which nothing writes to
run xip 'mount;cat /proc/self/maps;free\n' "${xip[@]}" -flash "$OUT/xip-after.bin" -flash-writes
log=$OUT/boot-test-xip.log
grep -q '^mtd0 on / type romfs' "$log" || fail xip "the root file system is not the romfs"
grep -qE '^00[0-9a-f]{6}-[0-9a-f]{8} r-xp .* /bin/cat$' "$log" || fail xip "cat does not run from the flash"
grep -q '^==== flash writes: 0 ====$' "$log" || fail xip "something wrote to the flash"
free=$(awk '$1=="Mem:"{f=$4} END{print f}' "$log")
echo "free memory with the kernel in place: $free KiB"
[ "${free:-0}" -gt 1024 ] || fail xip "less free memory than expected"

# 5. F6 removes it: the flash is as it was before the install
run xip-remove 'mount\n' "${xip[@]}" -getkey 30014 -flash "$OUT/xip-after.bin" -flash-out "$OUT/xip-removed.bin"
grep -q '^Linux is removed from the flash.$' "$OUT/boot-test-xip-remove.log" || fail xip-remove "not removed"
check_flash "$OUT/xip-before.bin" "$OUT/xip-removed.bin" - || fail xip-remove "the flash is not as before"

echo "PASS (screens: $OUT/boot-test-*.png)"
