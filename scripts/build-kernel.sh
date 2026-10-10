#!/bin/bash
# Build the kernel with the BusyBox initramfs built in.
# Output: $OUT/linux.bin (zImage), $OUT/kernel.release, $OUT/font6x8.h
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
B=$WORK/linux-build
RAM_START=0x08000000
RAM_SIZE=0x00200000
HEAP_SIZE=0x10000		# arch/sh/boot/compressed/misc.c

K=(make -C "$SRC" O="$B" ARCH=sh CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS")
mkdir -p "$B"
"${K[@]}" fxcg20_defconfig
CONFIGS="$TOP/config/kernel.config"
[ "$DEBUG" = 1 ] && CONFIGS="$CONFIGS $TOP/config/kernel-debug.config"
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$B" "$B/.config" $CONFIGS
"$SRC/scripts/config" --file "$B/.config" --set-str INITRAMFS_SOURCE "$OUT/initramfs.list"
"${K[@]}" olddefconfig

# Fail if a requested option did not make it into the configuration
grep -h '^CONFIG_' $CONFIGS | while IFS= read -r line; do
	grep -qxF "$line" "$B/.config" || { echo "missing in .config: $line" >&2; exit 1; }
done

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

cp "$B/arch/sh/boot/zImage" "$OUT/linux.bin"
make -s -C "$SRC" O="$B" ARCH=sh kernelrelease > "$OUT/kernel.release"
# the console font, for the boot test to read the screen
cp "$SRC/arch/sh/boards/mach-fxcg/font6x8.h" "$OUT/font6x8.h"
echo "kernel $(cat "$OUT/kernel.release"): linux.bin $(stat -c %s "$OUT/linux.bin") bytes, BOOT_LINK_OFFSET=$off"
