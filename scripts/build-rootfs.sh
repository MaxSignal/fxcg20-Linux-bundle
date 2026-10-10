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
sed -e "s|@ROOTFS@|$TOP/rootfs|g" -e "s|@BUSYBOX@|$OUT/busybox|g" \
	-e "s|@LINKS@|$links|g" "$TOP/rootfs/initramfs.list.in" > "$OUT/initramfs.list"
echo "busybox: $size bytes, $(wc -l < busybox.links) applets"
