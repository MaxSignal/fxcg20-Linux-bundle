Linux @KERNEL_RELEASE@ and BusyBox @BUSYBOX@ for the Casio fx-CG10 / fx-CG20.

## Installation

1. **Back up the calculator first.** Linux uses all of the RAM, so the Main
   Memory may be lost. The storage memory is never written (F2/F6 below
   write only the free flash after the Casio OS image).
2. Copy `fxcglinux.g3a` and `linux.bin` (from the ZIP) to the root of the
   storage memory over USB.
3. Run **Linux** from the main menu and press **EXE** when asked.
4. To go back to the Casio OS, press the **RESET** button on the back (or
   run `reboot`).

### More free RAM: the kernel in the flash

Copy `xip.bin` to the storage memory too, run **Linux** and press **F2**:
`linux.bin` boots, writes the kernel and its romfs root file system to the
free flash after the Casio OS image (below 0xb40000) and starts them. They
run in place from there, which leaves about 1.3 MiB of RAM free instead
of 330 KiB. From then on **F2** (or EXE) boots it directly, **F1** boots
`linux.bin`, **F6** removes it (the flash is erased again, as before). A
new `xip.bin` is installed by F2 the same way. A Casio OS update may erase
it; nothing else uses that part of the flash, and the installer only
ever erases sectors that are erased already or hold its own image.

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
