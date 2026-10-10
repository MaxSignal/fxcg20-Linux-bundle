#!/bin/bash
# Zip what goes onto the calculator: $OUT/fxcg20-linux-<kernel>.zip
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/versions.sh

name=fxcg20-linux-$(cat "$OUT/kernel.release")
[ "$DEBUG" = 1 ] && name=$name-debug
zip=$OUT/$name.zip
python3 - "$zip" "$OUT/fxcglinux.g3a" "$OUT/linux.bin" "$OUT/xip.bin" <<'PY'
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

More free RAM (about 1.3 MiB instead of 0.3): the kernel and the programs
can run in place from the free flash after the Casio OS image (below
0xb40000; a Casio OS update may erase it, nothing else uses it).
5. Copy xip.bin to the storage memory as well, boot Linux as above and
   type:  cat /mnt/fls/xip.bin > /dev/xipflash && echo OK
   (OK when it is written and checked; dmesg says more).
6. From then on the loader offers it: EXE boots it, F6 boots linux.bin
   (which can install a newer xip.bin the same way).

https://github.com/MaxSignal/fxcg20-Linux-bundle
""")
PY
echo "$zip"
