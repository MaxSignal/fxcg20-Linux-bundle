Linux @KERNEL_RELEASE@ and BusyBox @BUSYBOX@ for the Casio fx-CG10 / fx-CG20.

## Installation

1. **Back up the calculator first.** Linux uses all of the RAM, so the Main
   Memory may be lost. The storage memory is never written.
2. Copy `fxcglinux.g3a` and `linux.bin` (from the ZIP) to the root of the
   storage memory over USB.
3. Run **Linux** from the main menu and press **EXE** when asked.
4. To go back to the Casio OS, press the **RESET** button on the back (or
   run `reboot`).

The fx-CG50 is not supported yet (the loader refuses to run on it).

> This build boots on the emulator in CI. It has not been tested on a real
> calculator yet.

## Keys

`ALPHA` + key types the red letter (lower case), `SHIFT` `ALPHA` locks
letters, `x²` toggles capitals, `MENU` is Ctrl. `EXE` = Enter, `DEL` =
Backspace, `AC/ON` = Ctrl-C, `OPTN` = Tab, `EXIT` = Esc,
`F1`-`F6` = `| > < & ; $`. The full table is in the README.

## Versions

- Kernel: @KERNEL@
- Loader: @LOADER@
- BusyBox: @BUSYBOX@
- Toolchain: @TOOLCHAIN@

## SHA-256

```
@SHA256@
```
