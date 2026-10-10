#!/bin/bash
# Build the SH7305 emulator used by the boot test. Output: $WORK/cgemu
# (needs Go)
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

SRC=$(source_tree casio-cg50 "$EMU_REPO" "$EMU_REF" "${EMU_SRC:-}")
go -C "$SRC/emu_go" build -o "$WORK/cgemu" .
echo "emulator: $WORK/cgemu"
