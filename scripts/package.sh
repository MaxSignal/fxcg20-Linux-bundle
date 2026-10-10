#!/bin/bash
# Zip what goes onto the calculator: $OUT/fxcg20-linux-<kernel>.zip
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

zip=$OUT/fxcg20-linux-$(cat "$OUT/kernel.release").zip
python3 - "$zip" "$OUT/fxcglinux.g3a" "$OUT/linux.bin" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sys.argv[2:]:
        z.write(f, f.rsplit('/', 1)[-1])
    z.writestr('README.txt', """Linux for the Casio fx-CG10 / fx-CG20

1. Back up your calculator (Main Memory may be lost).
2. Copy fxcglinux.g3a and linux.bin to the root of the storage memory.
3. Run "Linux" from the main menu and press EXE when asked.
4. To go back to the Casio OS, press the RESET button on the back
   (or run "reboot").

https://github.com/MaxSignal/fxcg20-Linux-bundle
""")
PY
echo "$zip"
