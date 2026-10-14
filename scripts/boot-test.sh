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

# 4. EXE (F2) with flash.bin in the storage memory: linux.bin installs it.
# Only the two flash areas may change.
python3 scripts/fls.py mkimage "$OUT/rom-before.bin" flash.bin="$OUT/flash.bin" > /dev/null
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

# 5. EXE (F2) boots linuxf.bin; nothing writes to the flash
run rom 'free\n' "${rom[@]}" -flash "$OUT/rom-after.bin" -flash-writes
log=$OUT/boot-test-rom.log
grep -q '^==== flash writes: 0 ====$' "$log" || fail rom "something wrote to the flash"
free=$(awk '$1=="Mem:"{f=$4} END{print f}' "$log")
echo "free memory with part of the kernel in the flash: $free KiB"
[ "${free:-0}" -gt 700 ] || fail rom "less free memory than expected"

# 6. F6 removes the flash part
run rom-remove 'echo done\n' "${rom[@]}" -getkey 30014 -flash "$OUT/rom-after.bin" -flash-out "$OUT/rom-removed.bin"
grep -q '^The flash part is removed.$' "$OUT/boot-test-rom-remove.log" || fail rom-remove "not removed"
check_flash "$OUT/rom-before.bin" "$OUT/rom-removed.bin" - || fail rom-remove "the flash is not as before"

echo "PASS (screens: $OUT/boot-test-*.png)"
