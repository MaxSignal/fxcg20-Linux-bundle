#!/usr/bin/env python3
"""Make a romfs image from a gen_init_cpio list (the initramfs.list the
build writes): the root file system that the XIP kernel mounts in place
from the flash, so that programs run straight from it.

  mkromfs.py LIST OUT

Lines: dir PATH MODE UID GID / nod PATH MODE UID GID c|b MAJOR MINOR /
file PATH SOURCE MODE UID GID [HARD LINKS...] / slink PATH TARGET MODE
UID GID. romfs keeps no owners and only an executable bit; hard links
cost one 16-byte header (more for long names), file data starts on a
16-byte boundary.
"""
import struct
import sys

T_HARD, T_DIR, T_FILE, T_SLINK, T_BLK, T_CHR = 0, 1, 2, 3, 4, 5
EXEC = 8


def pad16(b):
    return b + b"\0" * (-len(b) % 16)


def checksum_fix(b):
    """the value that makes the big-endian 32-bit words of b sum to 0"""
    s = sum(struct.unpack(f">{len(b) // 4}I", b)) & 0xFFFFFFFF
    return (-s) & 0xFFFFFFFF


class Node:
    def __init__(self, name, kind, mode=0o755, data=b"", spec=0, target=None):
        self.name, self.kind, self.mode = name, kind, mode
        self.data, self.spec, self.target = data, spec, target
        self.children = {}      # name -> Node, for directories
        self.off = None


def parse(list_path):
    root = Node("", T_DIR)

    def parent_of(path):
        parts = [p for p in path.split("/") if p]
        d = root
        for p in parts[:-1]:
            d = d.children[p]
        return d, parts[-1]

    links = []
    for line in open(list_path):
        w = line.split()
        if not w or w[0].startswith("#"):
            continue
        if w[0] == "dir":
            d, n = parent_of(w[1])
            d.children.setdefault(n, Node(n, T_DIR, int(w[2], 8)))
        elif w[0] == "nod":
            d, n = parent_of(w[1])
            kind = T_CHR if w[5] == "c" else T_BLK
            d.children[n] = Node(n, kind, int(w[2], 8),
                                 spec=int(w[6]) << 16 | int(w[7]))
        elif w[0] == "file":
            d, n = parent_of(w[1])
            f = Node(n, T_FILE, int(w[3], 8), data=open(w[2], "rb").read())
            d.children[n] = f
            links += [(p, f) for p in w[6:]]
        elif w[0] == "slink":
            d, n = parent_of(w[1])
            d.children[n] = Node(n, T_SLINK, 0o777, data=w[2].encode())
        else:
            raise SystemExit(f"{list_path}: unknown entry {w[0]}")
    for path, f in links:
        d, n = parent_of(path)
        d.children[n] = Node(n, T_HARD, target=f)
    return root


def header_size(node):
    return 16 + len(pad16(node.name.encode() + b"\0"))


def layout(root):
    """Give every header its offset: a directory's entries follow it,
    "." and ".." first, then its files, then its subdirectories."""
    order = []

    def walk(d, parent):
        dot = Node(".", T_HARD, target=d)
        dotdot = Node("..", T_HARD, target=parent)
        entries = [dot, dotdot] + sorted(d.children.values(), key=lambda n: n.name)
        d.entries = entries
        for e in entries:
            order.append(e)
        for e in entries[2:]:
            if e.kind == T_DIR:
                walk(e, d)

    head = pad16(b"-rom1fs-" + b"\0" * 8 + b"rootfs\0")
    off = len(head)
    walk(root, root)
    for e in order:
        e.off = off
        size = header_size(e)
        if e.kind in (T_FILE, T_SLINK):
            size += len(pad16(e.data))
        off += size
    root.off = root.entries[0].off      # "." of the root: the root itself
    return head, order, off


def build(root):
    head, order, total = layout(root)
    img = bytearray(total)
    img[:len(head)] = head
    for d in [root] + [e for e in order if e.kind == T_DIR]:
        for i, e in enumerate(d.entries):
            nxt = d.entries[i + 1].off if i + 1 < len(d.entries) else 0
            kind, spec, size = e.kind, e.spec, 0
            if e is root.entries[0]:
                # the first header is the root itself: a directory whose
                # entries start with it (the kernel follows hard links here)
                kind, spec = T_DIR, e.off
            elif kind == T_HARD:
                tgt = e.target
                # a link to a directory points at its own header
                spec = tgt.entries[0].off if tgt.kind == T_DIR and tgt is root else tgt.off
            elif kind == T_DIR:
                spec = e.entries[0].off
            elif kind in (T_FILE, T_SLINK):
                size = len(e.data)
            flags = EXEC if (e.mode & 0o111 or kind == T_DIR) else 0
            name = pad16(e.name.encode() + b"\0")
            h = bytearray(struct.pack(">IIII", nxt | kind | flags, spec, size, 0) + name)
            struct.pack_into(">I", h, 12, checksum_fix(bytes(h)))
            img[e.off:e.off + len(h)] = h
            if kind in (T_FILE, T_SLINK):
                o = e.off + len(h)
                img[o:o + len(e.data)] = e.data
    img = bytearray(pad16(bytes(img)))
    struct.pack_into(">I", img, 8, len(img))
    struct.pack_into(">I", img, 12, checksum_fix(bytes(img[:min(512, len(img))])))
    return bytes(img)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    img = build(parse(sys.argv[1]))
    open(sys.argv[2], "wb").write(img)
    print(f"{sys.argv[2]}: romfs, {len(img)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
