#!/bin/bash
# Build the loader add-in. Output: $OUT/fxcglinux.g3a
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

SRC=$(source_tree fxcg-linux-loader "$LOADER_REPO" "$LOADER_REF" "${LOADER_SRC:-}")
make -C "$SRC" CROSS_COMPILE="$CROSS_COMPILE" clean all
cp "$SRC/fxcglinux.g3a" "$OUT/"
git -C "$SRC" rev-parse HEAD > "$OUT/loader.commit" 2>/dev/null || true
echo "loader: $OUT/fxcglinux.g3a"
