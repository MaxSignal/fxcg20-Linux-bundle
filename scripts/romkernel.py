#!/usr/bin/env python3
"""Part of the kernel's code in the flash (CONFIG_FXCG_ROM_TEXT): the code
and constants of chosen object files go to 0xc00000-0xc7ffff and
0xb20000-0xb3ffff, the rest of the kernel stays in RAM.

  romkernel.py pack VMLINUX.MAP OUT.lds
        from the map of a link, the size of the .text and .rodata each
        object keeps; pack the biggest first into the two areas and write
        the linker script fragment (fxcg-rom.lds) that puts them there
  romkernel.py refine VMLINUX.MAP FXCG-ROM.LDS
        after a link with that fragment, fill what room is left in the two
        areas (their real sizes are in the map) with objects still in RAM
  romkernel.py image VMLINUX OBJCOPY FLASH.BIN LINUX.BIN
        cut the two areas out of vmlinux, give each its header, write
        FLASH.BIN (what /dev/xipflash installs) and append to LINUX.BIN
        (the RAM part, the zImage) the trailer the loader compares with
        the flash before booting it

Each area starts with a 64-byte header: "fxcgROM1", its address and
length (header included), the CRC-32 of the rest, and the CRC-32 of the
header. FLASH.BIN is a 64-byte index ("fxcgROMI", count, then address
and length of each area, CRC-32 of the index) followed by the area
images, headers included. The trailer of LINUX.BIN (64 bytes):
"fxcgROMK", count, then address, length and CRC-32 of each area's
contents after the header, and a CRC-32 of the trailer.
"""
import re
import struct
import subprocess
import sys
import tempfile
import zlib

AREAS = [(0x00C00000, 0x80000), (0x00B20000, 0x20000)]   # address, size
NAMES = [".fxcg.rom0", ".fxcg.rom1"]
HEADER = 0x40
MARGIN = 0.03           # alignment padding between input sections

# kept in RAM: start-up, exception, cache and MMU code (arch/), and what is
# linked outside vmlinux.a
KEEP_IN_RAM = re.compile(r"^(arch/|init/version)")


def object_sizes(map_path):
    """size of the .text* and .rodata* input sections each object keeps in
    the link, from ld's map"""
    sizes = {}
    out = None
    pending = None          # an input section name on its own line
    for line in open(map_path, errors="replace"):
        if line and not line[0].isspace():
            out = line.split()[0] if line.strip() else out
            pending = None
            continue
        if out not in (".text", ".rodata") + tuple(NAMES):
            continue
        f = line.split()
        if not f:
            continue
        if len(f) == 1 and f[0].startswith("."):
            pending = f[0]
            continue
        if f[0].startswith(".") and len(f) >= 4:
            name, addr, size, obj = f[0], f[1], f[2], " ".join(f[3:])
        elif pending and f[0].startswith("0x") and len(f) >= 3:
            name, addr, size, obj = pending, f[0], f[1], " ".join(f[2:])
        else:
            pending = None
            continue
        pending = None
        if not (name.startswith(".text") or name.startswith(".rodata")):
            continue
        # thin archive members appear under their own paths
        m = re.fullmatch(r"(?:.*vmlinux\.a\()?([\w./+-]+\.o)\)?", obj)
        if not m:
            continue
        sizes[m.group(1)] = sizes.get(m.group(1), 0) + int(size, 16)
    return sizes


def area_sizes(map_path):
    """the size of the two output sections in a link's map"""
    out = {}
    for line in open(map_path, errors="replace"):
        f = line.split()
        if len(f) >= 3 and f[0] in NAMES and not line[0].isspace():
            out[f[0]] = int(f[2], 16)
    return [out.get(n, HEADER) for n in NAMES]


def lds_objects(lds_path):
    chosen, i = [[] for _ in AREAS], -1
    for line in open(lds_path):
        f = line.split()
        if f and f[0] in NAMES:
            i = NAMES.index(f[0])
        m = re.match(r"\s*([\w./+-]+)\.\[o\]\(", line)
        if m and i >= 0:
            chosen[i].append(m.group(1) + ".o")
    return chosen


def fill(sizes, chosen, room):
    in_rom = {o for c in chosen for o in c}
    for obj, size in sorted(sizes.items(), key=lambda kv: -kv[1]):
        if KEEP_IN_RAM.match(obj) or size == 0 or obj in in_rom:
            continue
        for i in range(len(AREAS)):
            if size <= room[i]:
                chosen[i].append(obj)
                room[i] -= size
                break


def pack(map_path, out_lds, refine=False):
    sizes = object_sizes(map_path)
    if refine:
        chosen = lds_objects(out_lds)
        used = area_sizes(map_path)
        # merged strings and alignment take more than the sum
        room = [int((size - u) * 0.85) for (_, size), u in zip(AREAS, used)]
    else:
        chosen = [[] for _ in AREAS]
        room = [int((size - HEADER) * (1 - MARGIN)) for _, size in AREAS]
    fill(sizes, chosen, room)
    lines = ["/* written by fxcg20-Linux-bundle scripts/romkernel.py pack */"]
    for i, ((addr, size), name) in enumerate(zip(AREAS, NAMES)):
        lines.append(f"{name} {addr:#010x} : AT({addr:#010x}) {{")
        lines.append(f"\t. = . + {HEADER:#x};\t/* the header */")
        for obj in sorted(chosen[i]):
            # a pattern (".[o]"), so that ld only matches the archive's
            # member and does not open the file as one more input
            lines.append(f"\t{obj[:-1]}[o](.text .text.* .rodata .rodata.*)")
        lines.append("}")
        lines.append(f'ASSERT(SIZEOF({name}) <= {size:#x}, "fxcg: {name} does not fit at {addr:#x}")')
    open(out_lds, "w").write("\n".join(lines) + "\n")
    total = sum(sizes[o] for c in chosen for o in c)
    left = sum(s for o, s in sizes.items() if not any(o in c for c in chosen))
    print(f"{out_lds}: {total >> 10} KiB of code and constants in the flash "
          f"({len(chosen[0])} + {len(chosen[1])} objects), {left >> 10} KiB left in RAM")


def header(addr, image):
    h = b"fxcgROM1" + struct.pack(">III", addr, len(image), zlib.crc32(image[HEADER:]))
    h += b"\xff" * (HEADER - 4 - len(h))
    return h + struct.pack(">I", zlib.crc32(h))


def image(vmlinux, objcopy, flash_bin, linux_bin):
    areas = []
    for (addr, size), name in zip(AREAS, NAMES):
        with tempfile.NamedTemporaryFile() as t:
            subprocess.run([objcopy, "-O", "binary", "-j", name, vmlinux, t.name], check=True)
            data = bytearray(open(t.name, "rb").read())
        if len(data) < HEADER:
            data = bytearray(HEADER)
        if len(data) & 1:
            data.append(0xFF)       # the flash is programmed in 16-bit words
        if len(data) > size:
            raise SystemExit(f"{name}: {len(data):#x} bytes, more than {size:#x}")
        data[0:HEADER] = header(addr, bytes(data))
        areas.append((addr, bytes(data)))
    index = b"fxcgROMI" + struct.pack(">I", len(areas))
    for addr, data in areas:
        index += struct.pack(">II", addr, len(data))
    index += b"\xff" * (HEADER - 4 - len(index))
    index += struct.pack(">I", zlib.crc32(index))
    open(flash_bin, "wb").write(index + b"".join(d for _, d in areas))
    trailer = b"fxcgROMK" + struct.pack(">I", len(areas))
    for addr, data in areas:
        trailer += struct.pack(">III", addr, len(data), zlib.crc32(data[HEADER:]))
    trailer += b"\xff" * (HEADER - 4 - len(trailer))
    trailer += struct.pack(">I", zlib.crc32(trailer))
    with open(linux_bin, "ab") as f:
        f.write(trailer)
    for addr, data in areas:
        print(f"flash {addr:#x}: {len(data) >> 10} KiB")
    print(f"{flash_bin}: {sum(len(d) for _, d in areas) >> 10} KiB; trailer appended to {linux_bin}")


def main(a):
    if len(a) == 3 and a[0] in ("pack", "refine"):
        pack(a[1], a[2], a[0] == "refine")
    elif len(a) == 5 and a[0] == "image":
        image(*a[1:])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
