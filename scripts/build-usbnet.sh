#!/bin/bash
# Build ncmd, the network over USB in user space (lwIP), and test it on
# the build machine against a host side made of lwIP too.
# Output: $OUT/ncmd, $OUT/ncmtest (that test for the calculator)
set -euo pipefail
cd "$(dirname "$0")/.."
TOP=$(pwd)
. scripts/versions.sh

fetch "$LWIP_URL" "$LWIP_SHA256"
LWIP=$WORK/lwip-$LWIP_VERSION
if [ ! -d "$LWIP" ]; then
	rm -rf "$WORK/lwip.tmp" && mkdir "$WORK/lwip.tmp"
	(cd "$WORK/lwip.tmp" && unzip -q "$DL/$(basename "$LWIP_URL")")
	mv "$WORK/lwip.tmp/lwip-$LWIP_VERSION" "$LWIP" && rmdir "$WORK/lwip.tmp"
fi

B=$WORK/usbnet
mkdir -p "$B/host"
make -s -C "$TOP/usbnet" test LWIP="$LWIP" O="$B/host" CC=gcc
make -s -C "$TOP/usbnet" -B LWIP="$LWIP" O="$B" CC="${CROSS_COMPILE}gcc" \
	LDFLAGS="-static -Wl,--gc-sections -Wl,-z,max-page-size=4096 \
		 -Wl,-z,common-page-size=4096 -Wl,-z,stack-size=8192" \
	"$B/ncmd" "$B/ncmtest"
"${CROSS_COMPILE}strip" -o "$OUT/ncmd" "$B/ncmd"
"${CROSS_COMPILE}strip" -o "$OUT/ncmtest" "$B/ncmtest"
echo "ncmd: $(stat -c %s "$OUT/ncmd") bytes"
