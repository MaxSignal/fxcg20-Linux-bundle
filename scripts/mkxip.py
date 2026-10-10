#!/usr/bin/env python3
"""Pack the XIP kernel and the romfs root file system for the free NOR
flash between the Casio OS image and the language data at 0xb40000
(erased on the fx-CG10/fx-CG20 and the fx-CG50). From 0xb40000 down: a
header in the last 256 bytes, the kernel from a 128 KiB flash sector,
the romfs, and the image starts on the sector below with a copy of the
header, 256 bytes before the romfs (arch/sh/boards/mach-fxcg/xip.c and
xipflash.c read them).

  mkxip.py place KERNEL_SIZE ROMFS_SIZE
        print where the kernel and the romfs go ("KSTART RSTART", hex)
  mkxip.py pack VMLINUX.BIN KSTART ROMFS OUT [--flash IMAGE OUTFLASH]
        write OUT, the flash from the romfs start up to 0xb40000 (the
        header at its end says where it goes); with --flash, also a copy
        of the 32 MiB IMAGE (or "-" for an erased flash) with it in place
  mkxip.py info FILE    print the header at the end of FILE
"""
import struct
import sys
import zlib

END = 0xB40000          # the language data starts here
SECTOR = 0x20000
HEADER = END - 0x100
HEADER_SIZE = 64
MAGIC = b"fxcgXIP1"
FLASH_SIZE = 0x2000000
LOWEST = 0x720000       # above the end of the Casio OS images seen so far


def place(ksize, rsize):
    """kernel start, image start (the romfs is 256 bytes after it)"""
    kstart = (HEADER - ksize) & ~(SECTOR - 1)
    start = kstart - ((rsize + 0x100 + SECTOR - 1) & ~(SECTOR - 1))
    return kstart, start


def header(kstart, kernel, rstart, romfs):
    h = MAGIC + struct.pack(">IIIIIIII", HEADER_SIZE,
                            kstart, len(kernel), zlib.crc32(kernel),
                            rstart, len(romfs), zlib.crc32(romfs), kstart)
    h += b"\xff" * (60 - len(h))
    return h + struct.pack(">I", zlib.crc32(h))


def parse_header(h):
    if h[:8] != MAGIC or zlib.crc32(h[:60]) != struct.unpack(">I", h[60:64])[0]:
        raise SystemExit("no valid XIP header")
    size, ks, kl, kc, rs, rl, rc, entry = struct.unpack(">IIIIIIII", h[8:40])
    return dict(kstart=ks, ksize=kl, kcrc=kc, rstart=rs, rsize=rl, rcrc=rc,
                entry=entry)


def pack(kfile, kstart, rfile, out, flash=None, outflash=None):
    kernel = open(kfile, "rb").read()
    romfs = open(rfile, "rb").read()
    want_k, start = place(len(kernel), len(romfs))
    if kstart != want_k:
        raise SystemExit(f"kernel linked at {kstart:#x}, its place is {want_k:#x}")
    if start < LOWEST:
        raise SystemExit(f"does not fit: the image would start at {start:#x}")
    rstart = start + 0x100
    h = header(kstart, kernel, rstart, romfs)
    region = bytearray(b"\xff" * (END - start))
    region[0:HEADER_SIZE] = h
    region[0x100:0x100 + len(romfs)] = romfs
    region[kstart - start:kstart - start + len(kernel)] = kernel
    region[HEADER - start:HEADER - start + HEADER_SIZE] = h
    open(out, "wb").write(region)
    print(f"{out}: romfs {rstart:#x} ({len(romfs) >> 10} KiB), kernel "
          f"{kstart:#x} ({len(kernel) >> 10} KiB), header {HEADER:#x}; "
          f"{(END - start) >> 10} KiB of flash from {start:#x}")
    if flash:
        img = bytearray(b"\xff" * FLASH_SIZE if flash == "-" else open(flash, "rb").read())
        if any(b != 0xFF for b in img[start:END]) and flash != "-":
            print(f"warning: {flash} is not erased where the image goes")
        img[start:END] = region
        open(outflash, "wb").write(img)
        print(f"{outflash}: flash image with it in place")


def main(a):
    if len(a) == 3 and a[0] == "place":
        k, r = place(int(a[1], 0), int(a[2], 0))
        print(f"{k:#x} {r:#x}")
    elif len(a) >= 5 and a[0] == "pack":
        fl = a[a.index("--flash") + 1:a.index("--flash") + 3] if "--flash" in a else [None, None]
        pack(a[1], int(a[2], 0), a[3], a[4], *fl)
    elif len(a) == 2 and a[0] == "info":
        d = open(a[1], "rb").read()
        h = parse_header(d[-0x100:-0x100 + HEADER_SIZE] if len(d) < FLASH_SIZE else d[HEADER:HEADER + HEADER_SIZE])
        for k, v in h.items():
            print(f"{k:7} {v:#x}")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
