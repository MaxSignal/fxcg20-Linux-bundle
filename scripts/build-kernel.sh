#!/bin/bash
# Build the kernel with the BusyBox initramfs built in.
# Output: $OUT/linux.bin (zImage), $OUT/kernel.release, $OUT/font6x8.h
#
# ROM=1: the kernel with part of its code in the flash. Output:
# $OUT/linuxf.bin (RAM part) and $OUT/flash.bin (flash part). The objects
# that go to the flash are chosen from the map of a first link
# (scripts/romkernel.py).
#
# Memory budget: while the zImage decompresses, the compressed image and
# the kernel must both fit in the 2 MiB of RAM. The zImage is therefore
# linked as high as possible: it is built once to learn its size, then
# relinked at 2 MiB - (zImage + bss + decompressor heap).
set -euo pipefail
cd "$(dirname "$0")/.."
TOP=$(pwd)
. scripts/versions.sh

[ -f "$OUT/initramfs.list" ] || { echo "run scripts/build-rootfs.sh first" >&2; exit 1; }
SRC=$(source_tree linux "$KERNEL_REPO" "$KERNEL_REF" "${KERNEL_SRC:-}")
ROM=${ROM:-0}
B=$WORK/linux-build
[ "$ROM" = 1 ] && B=$WORK/linux-rom-build
RAM_START=0x08000000
RAM_SIZE=0x00200000
HEAP_SIZE=0x10000		# arch/sh/boot/compressed/misc.c

K=(make -C "$SRC" O="$B" ARCH=sh CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS")
mkdir -p "$B"
"${K[@]}" fxcg20_defconfig
CONFIGS="$TOP/config/kernel.config"
[ "$DEBUG" = 1 ] && CONFIGS="$CONFIGS $TOP/config/kernel-debug.config"
if [ "$ROM" = 1 ]; then CONFIGS="$CONFIGS $TOP/config/kernel-rom.config"
else CONFIGS="$CONFIGS $TOP/config/kernel-ram.config"; fi
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$B" "$B/.config" $CONFIGS
"$SRC/scripts/config" --file "$B/.config" --set-str INITRAMFS_SOURCE "$OUT/initramfs.list"
"${K[@]}" olddefconfig

# Fail if a requested option did not make it into the configuration (the
# last fragment that sets a symbol wins)
for f in $CONFIGS; do sed -n 's/^\(CONFIG_[A-Z0-9_]*\)=.*/\1 &/p; s/^# \(CONFIG_[A-Z0-9_]*\) is not set/\1 &/p' "$f"; done |
	awk '{v[$1]=substr($0, length($1) + 2)} END {for (k in v) print v[k]}' |
	while IFS= read -r line; do
		grep -qxF "$line" "$B/.config" || { echo "missing in .config: $line" >&2; exit 1; }
	done

if [ "$ROM" = 1 ]; then
	# a first link with nothing in the flash, for the map
	: > "$B/fxcg-rom.lds"
	rm -f "$B/vmlinux" "$B/vmlinux.unstripped"
	"${K[@]}" vmlinux
	python3 "$TOP/scripts/romkernel.py" pack "$B/vmlinux.map" "$B/fxcg-rom.lds"
	# and once more with the room the first guess left
	rm -f "$B/vmlinux" "$B/vmlinux.unstripped"	# ld's script changed
	"${K[@]}" vmlinux
	cp "$B/fxcg-rom.lds" "$B/fxcg-rom.lds.first"
	python3 "$TOP/scripts/romkernel.py" refine "$B/vmlinux.map" "$B/fxcg-rom.lds"
	rm -f "$B/vmlinux" "$B/vmlinux.unstripped"
	if ! "${K[@]}" vmlinux; then
		echo "the refined packing does not fit: back to the first one" >&2
		cp "$B/fxcg-rom.lds.first" "$B/fxcg-rom.lds"
		rm -f "$B/vmlinux" "$B/vmlinux.unstripped"
	fi
fi
"${K[@]}" zImage

place() {
	local nm=${CROSS_COMPILE}nm z=$B/arch/sh/boot/compressed
	local start end zsize out_end link
	start=$($nm "$z/vmlinux" | awk '$3=="startup"{print "0x"$1}')
	end=$($nm "$z/vmlinux" | awk '$3=="_end"{print "0x"$1}')
	zsize=$(( end - start + HEAP_SIZE ))
	out_end=$(( RAM_START + 0x2000 + $(stat -c %s "$z/vmlinux.bin") ))
	link=$(( (RAM_START + RAM_SIZE - zsize) & ~0xfff ))
	printf 'kernel image ends at 0x%08x, zImage needs 0x%x bytes: link at 0x%08x\n' \
		$out_end $zsize $link >&2
	if (( link < out_end )); then
		printf 'does not fit in RAM (short by %d KiB)\n' $(( (out_end - link) / 1024 )) >&2
		exit 1
	fi
	printf '0x%08x' $(( link - RAM_START ))
}

off=$(place)
"$SRC/scripts/config" --file "$B/.config" --set-val BOOT_LINK_OFFSET "$off"
"${K[@]}" olddefconfig
"${K[@]}" zImage
[ "$(place)" = "$off" ] || { echo "zImage size changed while relinking" >&2; exit 1; }

if [ "$ROM" = 1 ]; then
	cp "$B/arch/sh/boot/zImage" "$OUT/linuxf.bin"
	python3 "$TOP/scripts/romkernel.py" image "$B/vmlinux" "${CROSS_COMPILE}objcopy" \
		"$OUT/flash.bin" "$OUT/linuxf.bin"
	echo "kernel: linuxf.bin $(stat -c %s "$OUT/linuxf.bin") bytes, flash.bin $(stat -c %s "$OUT/flash.bin") bytes, BOOT_LINK_OFFSET=$off"
	exit 0
fi
cp "$B/arch/sh/boot/zImage" "$OUT/linux.bin"
make -s -C "$SRC" O="$B" ARCH=sh kernelrelease > "$OUT/kernel.release"
# the console font, for the boot test to read the screen
cp "$SRC/arch/sh/boards/mach-fxcg/font6x8.h" "$OUT/font6x8.h"
echo "kernel $(cat "$OUT/kernel.release"): linux.bin $(stat -c %s "$OUT/linux.bin") bytes, BOOT_LINK_OFFSET=$off"
