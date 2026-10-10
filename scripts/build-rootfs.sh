#!/bin/bash
# Build BusyBox (static FDPIC) and the initramfs list the kernel embeds.
# Output: $OUT/busybox, $OUT/initramfs.list
set -euo pipefail
cd "$(dirname "$0")/.."
TOP=$(pwd)
. scripts/versions.sh

fetch "$BUSYBOX_URL" "$BUSYBOX_SHA256"
BB=$WORK/busybox-$BUSYBOX_VERSION
[ -d "$BB" ] || tar xjf "$DL/$(basename "$BUSYBOX_URL")" -C "$WORK"

cd "$BB"
make allnoconfig >/dev/null
# merge the fragment: drop every symbol it sets, then append it
syms=$( (sed -n 's/^\(CONFIG_[A-Z0-9_]*\)=.*/\1/p' "$TOP/rootfs/busybox.fragment"; \
	echo CONFIG_CROSS_COMPILER_PREFIX) | paste -sd'|')
grep -v -E "^(# )?($syms)( is not set|=)" .config > .config.tmp || true
{ cat .config.tmp; cat "$TOP/rootfs/busybox.fragment"; \
  echo "CONFIG_CROSS_COMPILER_PREFIX=\"$CROSS_COMPILE\""; } > .config
rm -f .config.tmp
make oldconfig < <(yes "") >/dev/null
make -j"$JOBS" CROSS_COMPILE="$CROSS_COMPILE" busybox busybox.links
"${CROSS_COMPILE}strip" -o "$OUT/busybox" busybox

size=$(stat -c %s "$OUT/busybox")
# nommu runs the binary in place from the initramfs, which needs it in
# one physically contiguous block: keep it within an order-6 block.
if [ "$size" -gt $((256 * 1024)) ]; then
	echo "busybox is $size bytes, more than 256 KiB" >&2
	exit 1
fi

# Applets are hard links: a ramfs symlink costs a whole 4 KiB page.
links=$(grep -v '^/bin/busybox$' busybox.links | paste -sd' ')
inittab=$TOP/rootfs/etc/inittab
if [ "$DEBUG" = 1 ]; then
	inittab=$OUT/inittab
	cat "$TOP/rootfs/etc/inittab" "$TOP/rootfs/debug/inittab" > "$inittab"
fi
sed -e "s|@ROOTFS@|$TOP/rootfs|g" -e "s|@BUSYBOX@|$OUT/busybox|g" \
	-e "s|@LINKS@|$links|g" -e "s|@INITTAB@|$inittab|g" \
	"$TOP/rootfs/initramfs.list.in" > "$OUT/initramfs.list"

# DEBUG=1: the USB shell launcher, tiny so that it can wait on the port,
if [ "$DEBUG" = 1 ]; then
	"${CROSS_COMPILE}gcc" -Os -static -Wall -o "$OUT/usb-shell" \
		"$TOP/rootfs/debug/usb-shell.c" -Wl,-z,max-page-size=4096 \
		-Wl,-z,common-page-size=4096 -Wl,-z,stack-size=4096
	"${CROSS_COMPILE}strip" "$OUT/usb-shell"
	echo "file /sbin/usb-shell $OUT/usb-shell 755 0 0" >> "$OUT/initramfs.list"
	# and a read-only flash dumper for it
	"${CROSS_COMPILE}gcc" -Os -static -Wall -o "$OUT/flashdump" \
		"$TOP/rootfs/debug/flashdump.c" -Wl,-z,max-page-size=4096 \
		-Wl,-z,common-page-size=4096 -Wl,-z,stack-size=4096
	"${CROSS_COMPILE}strip" "$OUT/flashdump"
	echo "file /sbin/flashdump $OUT/flashdump 755 0 0" >> "$OUT/initramfs.list"
fi
echo "busybox: $size bytes, $(wc -l < busybox.links) applets"
