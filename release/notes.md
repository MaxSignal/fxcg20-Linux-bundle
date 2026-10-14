Linux @KERNEL_RELEASE@ and BusyBox @BUSYBOX@ for the Casio fx-CG10 / fx-CG20.

## Installation

1. Back up the Main Memory: Linux uses all of the RAM.
2. Copy `fxcglinux.g3a`, `linux.bin`, `linuxf.bin` and `flash.bin` to the
   root of the storage memory.
3. Run **Linux** from the main menu. **F1** boots `linux.bin`; **F2** boots
   `linuxf.bin` (the first time, `linux.bin` installs `flash.bin` and
   resets: run Linux again); **F6** removes the flash part.
4. To go back to the Casio OS, press **RESET** (or run `reboot`).

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
