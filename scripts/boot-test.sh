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

# 3. MENU + + (backlight up two steps), SHIFT AC/ON (sleep), AC/ON (wake up)
run keys "\\c++\\c\\s$(printf '\003')echo awake\\n"
grep -q '^awake$' "$OUT/boot-test-keys.log" || fail keys "no shell after waking up"
grep -q 'R5A1: 50 ' "$OUT/boot-test-keys.log" || fail keys "backlight"

# 4. a tone on the serial jack: 1000 Hz for 0.5 s, about 1000 changes of a line
run tone 'beep -f 1000 -l 500\n'
n=$(sed -n 's/^==== 3-pin transmit pin changes: \([0-9]*\) ====$/\1/p' "$OUT/boot-test-tone.log")
[ "${n:-0}" -ge 900 ] && [ "$n" -le 1100 ] || fail tone "$n changes of the transmit pin"

# 5. EXE (F2) with flash.bin in the storage memory: linux.bin installs it.
# Only the two flash areas may change.
python3 scripts/fls.py mkimage "$OUT/rom-before.bin" flash.bin="$OUT/flash.bin" \
	ncmtest="$OUT/ncmtest" > /dev/null
rom=(-fls linuxf.bin="$OUT/linuxf.bin",flash.bin="$OUT/flash.bin")
run rom-install 'dmesg | grep xipflash\n' "${rom[@]}" -flash "$OUT/rom-before.bin" -flash-out "$OUT/rom-after.bin"
grep -q '^xipflash: installed$' "$OUT/boot-test-rom-install.log" || fail rom-install "the install failed"
check_flash() {	# BEFORE AFTER FLASH.BIN|-: skipping the loader's page at 0x300000
	python3 - "$@" <<'PY'
import struct, sys
before, after = (open(f, "rb").read() for f in sys.argv[1:3])
areas = [(0xC00000, 0xC80000), (0xB20000, 0xB40000)]
if sys.argv[3] == "-":
    for lo, hi in areas:
        assert after[lo:hi] == b"\xff" * (hi - lo), f"{lo:#x} is not erased"
else:
    f = open(sys.argv[3], "rb").read()
    off = 64
    for i in range(struct.unpack(">I", f[8:12])[0]):
        addr, n = struct.unpack(">II", f[12 + 8 * i:20 + 8 * i])
        assert after[addr:addr + n] == f[off:off + n], f"{addr:#x} differs from flash.bin"
        off += n
keep = [(0, 0x300000), (0x310000, 0xB20000), (0xB40000, 0xC00000), (0xC80000, len(after))]
for lo, hi in keep:
    assert after[lo:hi] == before[lo:hi], f"the flash changed in {lo:#x}-{hi:#x}"
PY
}
check_flash "$OUT/rom-before.bin" "$OUT/rom-after.bin" "$OUT/flash.bin" || fail rom-install "the flash"

# 6. EXE (F2) boots linuxf.bin; nothing writes to the flash; ncmd runs
run rom 'free; cat /proc/[0-9]*/comm\n' "${rom[@]}" -flash "$OUT/rom-after.bin" -flash-writes
log=$OUT/boot-test-rom.log
grep -q '^==== flash writes: 0 ====$' "$log" || fail rom "something wrote to the flash"
grep -q '^ncmd$' "$log" || fail rom "ncmd (network over USB) is not running"
free=$(awk '$1=="Mem:"{f=$4} END{print f}' "$log")
echo "free memory with part of the kernel in the flash: $free KiB"
[ "${free:-0}" -gt 700 ] || fail rom "less free memory than expected"

# 7. ncmd on the calculator (linuxf.bin) against ncmtest, the computer's
# side made of lwIP (the emulator has no USB): DHCP, DNS, pings, tcp
run net 'cp /mnt/fls/ncmtest /tmp/t; /tmp/t /sbin/ncmd /tmp/n\n' "${rom[@]}" -flash "$OUT/rom-after.bin" -ins 8000000000
grep -q '^PASS$' "$OUT/boot-test-net.log" || fail net "ncmtest"

# 8. the storage memory, written by linuxf.bin (part of the kernel runs
# from the flash meanwhile) on a flash with only 8 erased storage sectors
# left, so that it also collects (copies, logs and erases) sectors.
# fls.py checks every record written and the FAT16 disk; nothing outside
# the storage area may change.
python3 scripts/fls.py mkimage "$OUT/fls-before.bin" --base="$OUT/rom-after.bin" --free=8 > /dev/null
run fls 'cd /mnt/fls;echo hello > a.txt;mkdir Dir;cp /bin/busybox Dir/LongFileName.bin;cp a.txt b.txt;mv b.txt Dir/;rm a.txt;echo more >> Dir/b.txt;sync;md5sum /bin/busybox Dir/*\n' \
	"${rom[@]}" -flash "$OUT/fls-before.bin" -flash-out "$OUT/fls-after.bin" -ins 12000000000
bb=$(sed -n 's|^\([0-9a-f]\{32\}\)  /bin/busybox$|\1|p' "$OUT/boot-test-fls.log")
[ -n "$bb" ] || fail fls "md5sum /bin/busybox"
grep -q "^$bb  Dir/LongFileName.bin" "$OUT/boot-test-fls.log" || fail fls "md5sum of the copy"
python3 scripts/fls.py check "$OUT/fls-after.bin" > "$OUT/boot-test-fls-check.log" ||
	{ cat "$OUT/boot-test-fls-check.log"; fail fls "the flash the kernel wrote"; }
grep -qE "  $bb +[0-9]+ /Dir/LongFileName.bin$" "$OUT/boot-test-fls-check.log" ||
	fail fls "the copy is not on the disk"
grep -q "  $(printf 'hello\nmore\n' | md5sum | cut -d' ' -f1)       11 /Dir/b.txt$" \
	"$OUT/boot-test-fls-check.log" || fail fls "Dir/b.txt"
grep -q " /a.txt$" "$OUT/boot-test-fls-check.log" && fail fls "a.txt was not removed"
grep -qE "erase log records: [1-9]" "$OUT/boot-test-fls-check.log" || fail fls "no sector was collected"
cmp -n $((0xc80000)) "$OUT/fls-before.bin" "$OUT/fls-after.bin" > /dev/null ||
	fail fls "the flash changed outside the storage area"

# 9. and it is all there after a reset
run fls-reread 'md5sum /mnt/fls/Dir/*;cat /mnt/fls/Dir/b.txt\n' "${rom[@]}" -flash "$OUT/fls-after.bin"
grep -q "^$bb  /mnt/fls/Dir/LongFileName.bin" "$OUT/boot-test-fls-reread.log" || fail fls-reread "md5sum"
grep -q '^more$' "$OUT/boot-test-fls-reread.log" || fail fls-reread "Dir/b.txt"

# 10. F6 removes the flash part
run rom-remove 'echo done\n' "${rom[@]}" -getkey 30014 -flash "$OUT/rom-after.bin" -flash-out "$OUT/rom-removed.bin"
grep -q '^The flash part is removed.$' "$OUT/boot-test-rom-remove.log" || fail rom-remove "not removed"
check_flash "$OUT/rom-before.bin" "$OUT/rom-removed.bin" - || fail rom-remove "the flash is not as before"

echo "PASS (screens: $OUT/boot-test-*.png)"
