#!/bin/bash
# Zip what goes onto the calculator: $OUT/fxcg20-linux-<kernel>.zip
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

name=fxcg20-linux-$(cat "$OUT/kernel.release")
[ "$DEBUG" = 1 ] && name=$name-debug
zip=$OUT/$name.zip
python3 - "$zip" "$OUT/fxcglinux.g3a" "$OUT/linux.bin" "$OUT/linuxf.bin" "$OUT/flash.bin" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sys.argv[2:]:
        z.write(f, f.rsplit('/', 1)[-1])
    z.writestr('README.txt', """Linux for the Casio fx-CG10 / fx-CG20

1. Back up the Main Memory: Linux uses all of the RAM.
2. Copy fxcglinux.g3a, linux.bin, linuxf.bin and flash.bin to the root of
   the storage memory.
3. Run "Linux" from the main menu:
     F1  boot linux.bin
     F2  boot linuxf.bin (the first time, linux.bin installs flash.bin
         and resets: run Linux again)
     F6  remove the flash part
4. To go back to the Casio OS, press RESET (or run "reboot").

https://github.com/MaxSignal/fxcg20-Linux-bundle
""")
PY
echo "$zip"
