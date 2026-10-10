#!/bin/bash
# Make $OUT/release-notes.md from release/notes.md
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

sums=$(cd "$OUT" && sha256sum fxcglinux.g3a linux.bin fxcg20-linux-*.zip)
kernel_commit=$(cut -c1-12 "$OUT/kernel.commit" 2>/dev/null || echo unknown)
loader_commit=$(cut -c1-12 "$OUT/loader.commit" 2>/dev/null || echo unknown)
python3 - "$OUT/release-notes.md" <<PY
import sys
t = open('release/notes.md').read()
for k, v in {
    '@KERNEL_RELEASE@': open('$OUT/kernel.release').read().strip(),
    '@KERNEL@': '$KERNEL_REPO'.removesuffix('.git') + ' \`$KERNEL_REF\` ($kernel_commit)',
    '@LOADER@': '$LOADER_REPO'.removesuffix('.git') + ' \`$LOADER_REF\` ($loader_commit)',
    '@BUSYBOX@': '$BUSYBOX_VERSION',
    '@TOOLCHAIN@': 'gcc $GCC_VERSION, binutils $BINUTILS_VERSION, musl $MUSL_VERSION',
    '@SHA256@': '''$sums''',
}.items():
    t = t.replace(k, v)
open(sys.argv[1], 'w').write(t)
PY
echo "$OUT/release-notes.md"
