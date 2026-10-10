#!/bin/bash
# Build the kernel that runs in place from the flash and the romfs root
# file system, packed for the free flash below 0xb40000.
# Output: $OUT/xip.bin (to install with "cat xip.bin > /dev/xipflash"),
#         $OUT/rootfs.romfs, $OUT/xip-flash.bin (an erased 32 MiB flash
#         with it in place, for the emulator)
#
# The kernel is linked at the address it runs from, which depends on its
# size: it is built once to learn the size, then relinked there.
set -euo pipefail
cd "$(dirname "$0")/.."
TOP=$(pwd)
. scripts/versions.sh

[ -f "$OUT/initramfs.list" ] || { echo "run scripts/build-rootfs.sh first" >&2; exit 1; }
SRC=$(source_tree linux "$KERNEL_REPO" "$KERNEL_REF" "${KERNEL_SRC:-}")
B=$WORK/linux-xip-build

python3 scripts/mkromfs.py "$OUT/initramfs.list" "$OUT/rootfs.romfs"

K=(make -C "$SRC" O="$B" ARCH=sh CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS")
mkdir -p "$B"
"${K[@]}" fxcg20_defconfig
CONFIGS="$TOP/config/kernel-xip.config"
[ "$DEBUG" = 1 ] && CONFIGS="$CONFIGS $TOP/config/kernel-debug.config"
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$B" "$B/.config" $CONFIGS
"$SRC/scripts/config" --file "$B/.config" --set-val XIP_PHYS_ADDR 0x00a00000
"${K[@]}" olddefconfig
grep -h '^CONFIG_' $CONFIGS | while IFS= read -r line; do
	grep -qxF "$line" "$B/.config" || { echo "missing in .config: $line" >&2; exit 1; }
done

"${K[@]}" vmlinux.bin
img=$B/arch/sh/boot/vmlinux.bin
size=$(stat -c %s "$img")
read -r kstart rstart < <(python3 scripts/mkxip.py place "$size" "$(stat -c %s "$OUT/rootfs.romfs")")
"$SRC/scripts/config" --file "$B/.config" --set-val XIP_PHYS_ADDR "$kstart"
"${K[@]}" olddefconfig
"${K[@]}" vmlinux.bin
[ "$(python3 scripts/mkxip.py place "$(stat -c %s "$img")" 0 | cut -d' ' -f1)" = "$kstart" ] ||
	{ echo "the kernel grew out of its place when relinked" >&2; exit 1; }

python3 scripts/mkxip.py pack "$img" "$kstart" "$OUT/rootfs.romfs" "$OUT/xip.bin" \
	--flash - "$OUT/xip-flash.bin"
make -s -C "$SRC" O="$B" ARCH=sh kernelrelease > "$OUT/kernel-xip.release"
cp "$SRC/arch/sh/boards/mach-fxcg/font6x8.h" "$OUT/font6x8.h"
