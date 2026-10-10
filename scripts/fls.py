#!/usr/bin/env python3
"""The Casio fx-CG10/fx-CG20 storage memory: the FAT16 disk the Casio OS
keeps in the NOR flash behind its flash translation layer, which Linux's
"fls" file system reads (arch/sh/boards/mach-fxcg/storage.c). The format
of its records (CRC-16 and Hamming codes, erase log) was worked out from
the data in flash dumps only: every record of a real fx-CG20's flash is
rebuilt byte for byte by make_record(). For the tests: images with given
files in the storage memory, and checks of an image.

  fls.py mkimage OUT [--base=IMAGE] [NAME=FILE...]
                            a 32 MiB flash image whose storage memory is a
                            FAT16 disk holding the files given (8.3 names,
                            root directory), the rest of the flash erased or
                            taken from IMAGE: test input for the emulator
  fls.py check IMAGE        check every record and sector of the flash
                            translation layer, then the FAT16 disk on it
                            (with fsck.fat when there is one); list the files
                            with their MD5
  fls.py cat IMAGE PATH     write a file of the storage memory to stdout
  fls.py disk IMAGE OUT     write the FAT16 disk (the 4 KiB pages) to OUT
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

FLASH_SIZE = 0x2000000
AREA_START = 0xC80000
SECTOR = 0x20000
NR_SECTORS = (FLASH_SIZE - AREA_START) // SECTOR
SLOT = 0x1000
NR_SLOTS = 30
RECORDS = 0x1F000
REC_HEADER, REC_DATA, REC_ERASED = 0x22, 0x11, 0x88
ERASES = 0x200      # erase counts in the slot of an erase log record
NR_PAGES = 0x1013   # 4 KiB pages of the 16 MiB FAT16 disk
MAX_PAGES = 0x1100  # the Casio OS also keeps a few pages past the disk
_PARITY = bytes(bin(i).count("1") & 1 for i in range(256))

# ---------------------------------------------------------------- records


def _parity(x):
    return bin(x).count("1") & 1


def hamming(data, bits):
    """Line parities (bit 2k: bytes whose address has bit k clear, 2k+1: set)
    and column parities of data seen as 2**bits bytes padded with 0xff."""
    addr = odd = col = 0
    par = bytes(data).translate(_PARITY)
    i = par.find(1)
    while i >= 0:
        addr ^= i
        odd ^= 1
        i = par.find(1, i + 1)
    for b in data:
        col ^= b
    lp = 0
    for k in range(bits):
        a = (addr >> k) & 1
        lp |= (odd ^ a) << (2 * k) | a << (2 * k + 1)
    cp = 0
    for k, (m0, m1) in enumerate(((0x55, 0xAA), (0x33, 0xCC), (0x0F, 0xF0))):
        cp |= _parity(col & m0) << (2 * k) | _parity(col & m1) << (2 * k + 1)
    return lp, cp


def _crc_table():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    return t


_CRC = _crc_table()


def crc16(data, c=0xFFFF):
    """CRC-16/CCITT-FALSE"""
    for b in data:
        c = (c << 8 & 0xFFFF) ^ _CRC[(c >> 8) ^ b]
    return c


def make_record(typ, count, page, data):
    """The 64-byte record of a slot holding data (padded with 0xff to 4 KiB)."""
    data = bytes(data) + b"\xff" * (SLOT - len(data))
    r = bytearray(b"\xff" * 64)
    r[0], r[1] = typ, 0x0F
    struct.pack_into(">IHH", r, 2, count, page, crc16(data))
    for i in range(8):
        lp, cp = hamming(data[512 * i:512 * i + 512], 9)
        r[10 + 3 * i:13 + 3 * i] = bytes(
            [~(cp << 2 | lp >> 16) & 0xFF, ~(lp >> 8) & 0xFF, ~lp & 0xFF])
    r[34:37] = b"\x5a\x5a\x5a"
    lp, cp = hamming(r[:34], 8)
    r[37:40] = bytes([~cp & 0x3F | 0xC0, ~lp & 0xFF, ~(lp >> 8) & 0xFF])
    return bytes(r)


def header(seq):
    return struct.pack(">II", ~seq & 0xFFFFFFFF, seq) * 3


def sector_off(sec):
    return AREA_START + sec * SECTOR


def slot_off(sec, slot):
    return sector_off(sec) + SLOT + slot * SLOT


def record_off(sec, n):
    """record n: 0 the header page, n + 1 slot n"""
    return sector_off(sec) + RECORDS + n * 64


def header_ok(h):
    return all(struct.unpack_from(">I", h, i)[0] ==
               ~struct.unpack_from(">I", h, i + 4)[0] & 0xFFFFFFFF
               for i in range(0, 24, 8))

# ------------------------------------------------------------- the image


def short_entry(name, attr, start, size):
    """a directory entry for an 8.3 name (lower case kept with the NT flags)"""
    base, _, ext = name.rpartition(".") if "." in name else (name, "", "")
    if not (1 <= len(base) <= 8 and len(ext) <= 3):
        raise SystemExit(f"{name}: not an 8.3 name")
    e = bytearray(32)
    e[0:11] = (base.upper().ljust(8) + ext.upper().ljust(3)).encode()
    e[11] = attr
    e[12] = (0x08 if base.islower() else 0) | (0x10 if ext.islower() else 0)
    struct.pack_into("<HHHI", e, 22, 0, 0x21, start, size)  # 1980-01-01
    return e


def mkimage(out, files=(), base=None):
    """A FAT16 disk with the geometry of the fx-CG20's storage memory, with
    files (name, data) in its root directory, in the storage area of a copy
    of the flash image base (erased if none); the rest of base is kept."""
    part, total, spf, roots, spc = 29, 32886, 17, 512, 8
    disk = bytearray(b"\xff" * (NR_PAGES * SLOT))
    mbr = bytearray(512)
    mbr[0x1BE:0x1CE] = struct.pack("<B3sB3sII", 0, b"\0\0\0", 0x0E,
                                   b"\0\0\0", part, total)
    mbr[510:512] = b"\x55\xaa"
    disk[0:512] = mbr
    disk[512:part * 512] = bytes((part - 1) * 512)
    bs = bytearray(512)
    bs[0:3] = b"\xeb\x3c\x90"
    bs[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, 512, spc, 1, 2, roots, total, 0xF8,
                     spf, 63, 255, part, 0)
    bs[36], bs[38] = 0x80, 0x29
    bs[43:62] = b"NO NAME    FAT16   "
    bs[510:512] = b"\x55\xaa"
    o = part * 512
    disk[o:o + 512] = bs
    fat = bytearray(spf * 512)
    fat[0:4] = b"\xf8\xff\xff\xff"
    root = bytearray(roots * 32)
    data_start = part + 1 + 2 * spf + roots * 32 // 512
    c = 2
    for i, (name, data) in enumerate(files):
        n = (len(data) + spc * 512 - 1) // (spc * 512)
        root[32 * i:32 * i + 32] = short_entry(name, 0x20, c if n else 0, len(data))
        for k in range(n):
            struct.pack_into("<H", fat, 2 * (c + k), c + k + 1 if k + 1 < n else 0xFFFF)
        o = (data_start + (c - 2) * spc) * 512
        disk[o:o + len(data)] = data
        c += n
    for i in range(2):
        o = (part + 1 + i * spf) * 512
        disk[o:o + len(fat)] = fat
    o = (part + 1 + 2 * spf) * 512
    disk[o:o + len(root)] = root
    flash = bytearray(open(base, "rb").read() if base else b"\xff" * FLASH_SIZE)
    flash[AREA_START:FLASH_SIZE] = b"\xff" * (FLASH_SIZE - AREA_START)
    pages = [p for p in range(NR_PAGES)
             if disk[p * SLOT:(p + 1) * SLOT] != b"\xff" * SLOT]
    sec, slot, seq = -1, NR_SLOTS, 0
    for p in pages:
        if slot == NR_SLOTS:
            sec, slot, seq = sec + 1, 0, seq + 1
            h = header(seq)
            flash[sector_off(sec):sector_off(sec) + 24] = h
            flash[record_off(sec, 0):record_off(sec, 0) + 64] = \
                make_record(REC_HEADER, 0xFFFFFFFF, 0xFFFF, h)
        data = disk[p * SLOT:(p + 1) * SLOT]
        flash[slot_off(sec, slot):slot_off(sec, slot) + SLOT] = data
        flash[record_off(sec, slot + 1):record_off(sec, slot + 1) + 64] = \
            make_record(REC_DATA, 0xFFFFFFFE, p, data)
        slot += 1
    with open(out, "wb") as f:
        f.write(flash)
    print(f"{out}: storage memory with {len(files)} files, {len(pages)} pages "
          f"in {sec + 1} sectors")


def check_ftl(img):
    """Check every sector and record; return (page -> data, problems)."""
    bad = []
    best = {}       # page -> (counter, offset)
    seqs = {}
    partial = []
    counters = {}
    erase_logs = {}
    for sec in range(NR_SECTORS):
        base = sector_off(sec)
        if not header_ok(img[base:base + 24]):
            if img[base:base + SECTOR] != b"\xff" * SECTOR:
                bad.append(f"sector {sec}: neither in use nor erased")
            continue
        seq = struct.unpack_from(">I", img, base + 4)[0]
        if seq in seqs:
            bad.append(f"sector {sec}: seq {seq} also in sector {seqs[seq]}")
        seqs[seq] = sec
        h = img[base:base + SLOT]
        if h[24:] != b"\xff" * (SLOT - 24):
            bad.append(f"sector {sec}: header page beyond the header not erased")
        r0 = img[record_off(sec, 0):record_off(sec, 0) + 64]
        if r0 != make_record(REC_HEADER, 0xFFFFFFFF, 0xFFFF, h):
            bad.append(f"sector {sec}: bad header record")
        used = 0
        for slot in range(NR_SLOTS):
            r = img[record_off(sec, slot + 1):record_off(sec, slot + 1) + 64]
            data = img[slot_off(sec, slot):slot_off(sec, slot) + SLOT]
            if r == b"\xff" * 64:
                if data != b"\xff" * SLOT:
                    bad.append(f"sector {sec} slot {slot}: data without a record")
                continue
            if slot != used:
                bad.append(f"sector {sec} slot {slot}: slots not taken in order")
            used = slot + 1
            count, page = struct.unpack_from(">IH", r, 2)
            if r != make_record(r[0], count, page, data):
                bad.append(f"sector {sec} slot {slot}: bad record {r[:10].hex()}")
                continue
            if r[0] == REC_DATA:
                if (page, count) in counters:
                    bad.append(f"page {page:#x}: counter {count:#x} twice")
                counters[(page, count)] = (sec, slot)
                if page not in best or count < best[page][0]:
                    best[page] = (count, slot_off(sec, slot))
            elif r[0] == REC_ERASED:
                n = struct.unpack_from(">H", data)[0]
                end = ERASES + 3 * NR_SECTORS
                if page != 0xFFFF or n >= NR_SECTORS or \
                        data[2:ERASES] + data[end:] != b"\xff" * (SLOT - 2 - 3 * NR_SECTORS):
                    bad.append(f"sector {sec} slot {slot}: bad erase log record")
                if count in erase_logs:
                    bad.append(f"erase log counter {count:#x} twice")
                erase_logs[count] = (n, [int.from_bytes(data[ERASES + 3 * i:ERASES + 3 * i + 3], "big")
                                         for i in range(NR_SECTORS)])
            else:
                bad.append(f"sector {sec} slot {slot}: record type {r[0]:#x}")
        if used < NR_SLOTS:
            partial.append((seq, sec))
    if len(partial) > 1:
        bad.append(f"more than one partly filled sector: {partial}")
    if partial and seqs and partial[0][0] != max(seqs):
        bad.append(f"the partly filled sector {partial[0]} is not the newest")
    for count, (n, table) in erase_logs.items():
        # one erase later, the same counts but one more for that sector
        if count + 1 in erase_logs:
            prev = list(erase_logs[count + 1][1])
            prev[n] += 1
            if prev != table:
                bad.append(f"erase log {count:#x}: counts do not follow {count + 1:#x}")
    pages = {p: img[o:o + SLOT] for p, (c, o) in best.items() if p < MAX_PAGES}
    free = sum(1 for s in range(NR_SECTORS)
               if img[sector_off(s):sector_off(s) + SECTOR] == b"\xff" * SECTOR)
    return pages, bad, len(seqs), free


def disk_image(pages):
    d = bytearray(b"\xff" * (MAX_PAGES * SLOT))
    for p, data in pages.items():
        d[p * SLOT:(p + 1) * SLOT] = data
    return bytes(d)

# ------------------------------------------------------------------ FAT16


class Fat16:
    def __init__(self, disk):
        self.d = disk
        part = 0
        if disk[510:512] == b"\x55\xaa" and disk[3:8] != b"MSDOS":
            part = struct.unpack_from("<I", disk, 0x1C6)[0]
        b = disk[part * 512:part * 512 + 512]
        self.part = part
        bps, self.spc, rsv, self.nfats, roots, tot16, _, self.spf = \
            struct.unpack_from("<HBHBHHBH", b, 11)
        self.total = tot16 or struct.unpack_from("<I", b, 32)[0]
        assert bps == 512
        self.fat = part + rsv
        self.root = self.fat + self.nfats * self.spf
        self.root_sectors = roots * 32 // 512
        self.data = self.root + self.root_sectors

    def sector(self, n):
        return self.d[n * 512:n * 512 + 512]

    def fat_entry(self, c):
        return struct.unpack_from("<H", self.d, self.fat * 512 + 2 * c)[0]

    def chain(self, c):
        out = []
        while 2 <= c < 0xFFF8 and len(out) < 0x10000:
            out.append(c)
            c = self.fat_entry(c)
        return out

    def dir_bytes(self, start):
        if start == 0:
            return self.d[self.root * 512:(self.root + self.root_sectors) * 512]
        return b"".join(self.d[(self.data + (c - 2) * self.spc) * 512:
                               (self.data + (c - 1) * self.spc) * 512]
                        for c in self.chain(start))

    def entries(self, start):
        raw = self.dir_bytes(start)
        lfn = {}
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                lfn = {}
                continue
            if e[11] == 0x0F:
                seq = e[0] & 0x1F
                chars = e[1:11] + e[14:26] + e[28:32]
                s = "".join(chr(struct.unpack_from("<H", chars, k)[0])
                            for k in range(0, 26, 2))
                lfn[seq] = s.split("\0")[0]
                continue
            if e[11] & 0x08 or e[:11] in (b".          ", b"..         "):
                lfn = {}
                continue
            base, ext = e[:8].decode("latin1").rstrip(), e[8:11].decode("latin1").rstrip()
            if e[12] & 0x08:
                base = base.lower()
            if e[12] & 0x10:
                ext = ext.lower()
            name = base + ("." + ext if ext else "")
            if lfn:
                name = "".join(lfn[k] for k in sorted(lfn))
            lfn = {}
            start_c, size = struct.unpack_from("<HI", e, 26)
            yield name, e[11], start_c, size

    def read(self, start, size):
        out = b"".join(self.d[(self.data + (c - 2) * self.spc) * 512:
                              (self.data + (c - 1) * self.spc) * 512]
                       for c in self.chain(start))
        return out[:size]

    def walk(self, start=0, path=""):
        for name, attr, c, size in self.entries(start):
            p = path + "/" + name
            if attr & 0x10:
                yield p + "/", attr, c, 0
                yield from self.walk(c, p)
            else:
                yield p, attr, c, size

    def find(self, path):
        start, attr, size = 0, 0x10, 0
        for part in [x for x in path.split("/") if x]:
            for name, a, c, s in self.entries(start):
                if name.lower() == part.lower():
                    start, attr, size = c, a, s
                    break
            else:
                raise FileNotFoundError(path)
        return start, attr, size


def fsck(fs):
    tool = shutil.which("fsck.fat") or shutil.which("dosfsck")
    if not tool:
        return None, "fsck.fat not found, skipped"
    with tempfile.NamedTemporaryFile(suffix=".img") as t:
        t.write(fs.d[fs.part * 512:(fs.part + fs.total) * 512])
        t.flush()
        p = subprocess.run([tool, "-n", t.name], capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr).strip()


def main(argv):
    if len(argv) >= 2 and argv[0] == "mkimage":
        base, files = None, []
        for arg in argv[2:]:
            if arg.startswith("--base="):
                base = arg[7:]
            else:
                name, _, path = arg.partition("=")
                files.append((name, open(path, "rb").read()))
        mkimage(argv[1], files, base)
        return 0
    if len(argv) < 2 or argv[0] not in ("check", "cat", "disk"):
        print(__doc__)
        return 2
    img = open(argv[1], "rb").read()
    if len(img) < FLASH_SIZE:
        img += b"\xff" * (FLASH_SIZE - len(img))
    pages, bad, used, free = check_ftl(img)
    fs = Fat16(disk_image(pages))
    if argv[0] == "cat":
        c, attr, size = fs.find(argv[2])
        sys.stdout.buffer.write(fs.read(c, size))
        return 0
    if argv[0] == "disk":
        open(argv[2], "wb").write(fs.d)
        return 0
    logs = sum(1 for sec in range(NR_SECTORS) for n in range(1, NR_SLOTS + 1)
               if header_ok(img[sector_off(sec):sector_off(sec) + 24]) and
               img[record_off(sec, n)] == REC_ERASED)
    print(f"flash translation layer: {used} sectors in use, {free} erased, "
          f"{len(pages)} pages mapped, erase log records: {logs}")
    for b in bad[:20]:
        print("  BAD:", b)
    if len(bad) > 20:
        print(f"  ... {len(bad) - 20} more")
    rc, out = fsck(fs)
    print("fsck.fat:", out.splitlines()[-1] if out else "")
    if rc not in (None, 0):
        print(out)
    for p, attr, c, size in fs.walk():
        if p.endswith("/"):
            print(f"  {'':32} {'':>8} {p}")
        else:
            print(f"  {hashlib.md5(fs.read(c, size)).hexdigest()} {size:8} {p}")
    return 1 if bad or rc not in (None, 0) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
