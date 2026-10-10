# fxcg20-linux-bundle

Builds everything needed to run Linux on a Casio fx-CG10 / fx-CG20 (Prizm):
the toolchain, the kernel with a BusyBox root filesystem built in, and the
loader add-in, boots them on an emulator, and zips the two files to copy to
the calculator. The fx-CG50 is not supported yet.

![BusyBox on the emulated fx-CG20](docs/emulator-shell.png)

| File | |
|---|---|
| `fxcglinux.g3a` | Loader add-in, started from the main menu |
| `linux.bin` | Kernel (zImage) with the BusyBox initramfs |

Both go to the root of the storage memory. The system is BusyBox 1.36
(hush, vi, coreutils, `ps`, `free`, `mount`…) on Linux 7.2, with the LCD
and the keyboard as the console. About 330 KiB of the 2 MiB of RAM are
free once the shell is up.

> **Status:** everything boots on the emulator in CI, down to running a
> command typed on the key matrix. It has not been tried on a real
> calculator yet.

## Components

| | Repository | Branch |
|---|---|---|
| Kernel | [MaxSignal/linux](https://github.com/MaxSignal/linux) | `fxcg20-7.2.y` (Linux 7.2.y stable + SH7305 / fx-CG patches) |
| Loader | [MaxSignal/fxcg-linux-loader](https://github.com/MaxSignal/fxcg-linux-loader) | `master` |
| Emulator (tests) | [MaxSignal/casio-cg50](https://github.com/MaxSignal/casio-cg50) | `linux-direct-boot` |
| Toolchain | built here (`scripts/build-toolchain.sh`): gcc 14.2, binutils 2.42, musl 1.2.4 | |

Versions and download locations are in `scripts/versions.sh`; each can be
overridden from the environment.

Linux 7.2 always builds the scheduler's SMP load balancing and debug code,
which makes it about 115 KiB larger than 6.12 for the same configuration.
It only fits because the kernel branch lets SH link with `--gc-sections`
(dead code and data elimination), which removes about 190 KiB of code that
nothing calls in this configuration.

## How it boots

1. The loader reads `linux.bin` and stages it in the RAM the Casio OS leaves
   to an add-in: the free part of the 512 KiB add-in RAM, blocks of the OS
   heap, and the VRAM. The physical address of every page comes from the
   UTLB.
2. After **EXE**, a trampoline copied to the on-chip IL memory masks every
   interrupt source, stops the timers and the watchdog, switches the MMU
   off, invalidates the caches, gathers the pieces at 0x08040000 and jumps
   to them.
3. The zImage moves itself to the top of RAM and decompresses the kernel to
   0x08002000 (`scripts/build-kernel.sh` computes that link address from the
   image sizes, so that both fit in the 2 MiB).
4. The kernel runs without the MMU. Its console draws text straight into
   the R61524 LCD controller's memory and polls the SH7305 key scanner.
5. The initramfs (gzip) holds one static FDPIC BusyBox with its applets as
   hard links; `/sbin/init` mounts `/proc`, `/dev` and `/tmp` and starts
   `hush`.

There is no way back to the Casio OS except a reset (the button on the back,
or `reboot`). Linux uses all of the RAM, so **the Main Memory may be lost:
back the calculator up first.** The storage memory is never written.

### Fitting in 2 MiB

- nommu: no page tables; programs are FDPIC and run in place from the
  initramfs, so BusyBox must be one physically contiguous block (the build
  checks it stays under 256 KiB). The kernel allocates its early memory
  bottom-up to keep the top of RAM free for it.
- The initramfs is compressed, so that the compressed image and the
  unpacked files fit at the same time.
- No framebuffer: the console renders text cells directly into the LCD.
- The kernel is linked with `--gc-sections` (.text 883 KiB down to 695 KiB).
- perf, hardware breakpoints, sysfs, block layer and networking are left
  out, and `min_free_kbytes` gets a lower floor on small nommu systems.

## Building

Debian/Ubuntu:

```sh
sudo apt install build-essential bison flex bc texinfo libgmp-dev libmpfr-dev \
                 libmpc-dev libssl-dev libelf-dev cpio python3 git curl
scripts/build-toolchain.sh  # work/toolchain (20-40 minutes, once)
scripts/build-rootfs.sh     # out/busybox, out/initramfs.list
scripts/build-kernel.sh     # out/linux.bin (needs the rootfs)
scripts/build-loader.sh     # out/fxcglinux.g3a
scripts/boot-test.sh        # boot test on the emulator (needs Go)
scripts/package.sh          # out/fxcg20-linux-<kernel>.zip
scripts/release-notes.sh    # out/release-notes.md
```

`KERNEL_SRC` / `LOADER_SRC` / `EMU_SRC` select local source trees instead
of cloning; `OUT`, `WORK` and `TOOLCHAIN` move the output, work and
toolchain directories.

The GitHub Actions workflow (`.github/workflows/build.yml`) does the same on
every push, keeps the ZIP as an artifact, and makes a release for each build
of `main` (tag `build-<run number>`) and each pushed `v*` tag.

### The emulator

The boot test uses [casio-cg50](https://github.com/hexbinoct/casio-cg50), an
SH7305 emulator that models the INTC, TMU, KEYSC, CPG and the R61524 LCD.
The `linux-direct-boot` branch adds a bare-metal fx-CG20 machine (2 MiB at
0x08000000, no Casio OS), the SH-4A instructions and exceptions Linux needs,
and an add-in runner that emulates the handful of OS syscalls the loader
uses. It reads the screen back as text with the kernel's font. To play with
it:

```sh
work/cgemu linux -g3a out/fxcglinux.g3a -file out/linux.bin -font out/font6x8.h \
    -ins 2000000000 -type 'ls -l /bin\n' -png screen.png
```

`-type` is typed with the key layout below (`\n` is EXE, `\t` OPTN, `\e`
EXIT), `-v` prints the screen as it changes, and `-kernel out/linux.bin`
starts the kernel without the loader. `scripts/boot-test.sh` boots twice:
once to run shell commands, once to edit a file with `vi`.

## Keys

The bottom line of the screen shows the modifiers.

| Key | Types | After SHIFT |
|---|---|---|
| digits, `.` `+` `-` `×` `÷` `(` `)` `,` | themselves (`×` is `*`, `÷` is `/`) | `+` `[`, `-` `]`, `×` `{`, `÷` `}`, `.` `=` |
| ×10ˣ | space | `"` |
| (−) | `_` | `%` |
| EXE / DEL / AC/ON | Enter / Backspace / Ctrl-C | – / Delete / Ctrl-D |
| arrows | cursor keys | Home, PgUp, PgDn, End |
| F1–F6 | `\|` `>` `<` `&` `;` `$` | `~` `%` `` ` `` `'` `:` `"` |
| X,θ,T / log / ln / sin / cos / tan | `=` `'` `"` `#` `!` `?` | |
| a b/c / S⇔D / → / ^ | `:` `@` `\` `^` | `^`: `` ` `` |
| OPTN / VARS / EXIT | Tab / `~` / Esc | |
| **ALPHA** | next key types its red letter, lower case | |
| SHIFT, ALPHA | letter lock (ALPHA again to leave) | |
| x² | caps lock | |
| MENU | next key with Ctrl (MENU, ALPHA, D is Ctrl-D) | |

For instance `ls` is `ALPHA` `→` `ALPHA` `×` `EXE`.

## Related work

- [gint](https://git.planet-casio.com/Lephenixnoir/gint) by Lephenixnoir, and
  the [casio-cg50](https://github.com/hexbinoct/casio-cg50) emulator by
  hexbinoct: SH7305 hardware documentation this port relies on.
- [linux-fx9750giii](https://github.com/cacioandpepe/linux-fx9750giii) by
  Artem Novak: SH7305 kernel work for the fx-9750GIII, and the upstream
  SH7305 patches on the kernel mailing list.
- [LinuX-CG50](https://github.com/Squaresweets/LinuX-CG50): Linux on an
  emulated RISC-V CPU (mini-rv32ima) as an fx-CG50 add-in, which runs out of
  RAM; this bundle runs Linux natively on the SH7305 instead.
- [stm32h7-linux](https://github.com/rota1001/stm32h7-linux): Linux with
  FDPIC BusyBox on an STM32H750 with 1 MiB of RAM.
- [nspire-Linux-bundle](https://github.com/MaxSignal/nspire-Linux-bundle):
  the same kind of bundle for the TI-Nspire.

## Licence

The scripts and configuration are GPL-2.0, like the kernel.
