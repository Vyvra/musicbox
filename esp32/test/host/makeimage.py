#!/usr/bin/env python3
"""Build small FAT16/FAT32 images with known contents, for testing fat_reader.

Deliberately independent of the C++ code: this writes the on-disk structures the FAT
specification defines, and `fsck.vfat -n` (dosfstools, a separate implementation) then
validates the result. Only if fsck is clean is the reader being tested against a
spec-correct volume rather than against its own assumptions.

Getting there took three corrections that fsck caught and I would not have:
  * long-name entries carry a checksum of the short name (byte 0x0D),
  * every subdirectory needs "." and ".." entries,
  * FAT32 wants an FSINFO sector and a backup boot sector.

Usage:  makeimage.py <out.img> <fat32|fat16> <size-mb>
"""
import struct
import sys

SECTOR = 512

# (path, content, force_long_name)
FILES = [
    ("01 - First Song.mp3", b"FIRST" * 200, False),
    ("10 - Tenth Song.mp3", b"TENTH" * 300, False),   # natural-sort trap: 10 vs 2
    ("02 - Second Song.mp3", b"SECOND" * 100, False),
    ("musicbox.txt", b'play_spotify_playlist="https://example/playlist"\nshuffle="on"\n', False),
    ("Sub Folder/deep track.mp3", b"DEEP" * 50, False),
    ("Een bestand met een lange naam.mp3", b"LONGNAME" * 80, True),
]

DOS_OFFSETS = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]


def short_name_checksum(name11: bytes) -> int:
    total = 0
    for byte in name11:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def name83(name):
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    return (base.upper()[:8].ljust(8) + ext.upper()[:3].ljust(3)).encode("ascii")


def needs_lfn(name):
    base, _, ext = name.rpartition(".")
    if not base:
        base, ext = name, ""
    return (len(base) > 8 or len(ext) > 3 or name != name.upper()
            or any(c in name for c in " -()+[],;="))


def build(out_path, kind, size_mb):
    total_sectors = (size_mb * 1024 * 1024) // SECTOR
    reserved = 32 if kind == "fat32" else 1
    fat_count = 2
    root_entries = 0 if kind == "fat32" else 512
    spc = 1
    bps = SECTOR
    cluster_bytes = spc * bps

    fat_sectors = max(1, (total_sectors // 100) + 32) if kind == "fat32" \
        else max(1, (total_sectors // 128) + 8)
    root_dir_sectors = ((root_entries * 32) + (bps - 1)) // bps
    first_data = reserved + fat_count * fat_sectors + root_dir_sectors
    cluster_count = (total_sectors - first_data) // spc
    root_cluster = 2 if kind == "fat32" else 0

    img = bytearray(total_sectors * SECTOR)

    # ---------------------------------------------------------------- boot sector
    def make_boot_sector():
        boot = bytearray(SECTOR)
        boot[0:3] = b"\xEB\x3C\x90"
        boot[3:11] = b"MSWIN4.1"
        struct.pack_into("<H", boot, 0x0B, bps)
        boot[0x0D] = spc
        struct.pack_into("<H", boot, 0x0E, reserved)
        boot[0x10] = fat_count
        struct.pack_into("<H", boot, 0x11, root_entries)
        if kind == "fat16":
            struct.pack_into("<H", boot, 0x13, min(total_sectors, 0xFFFF))
            struct.pack_into("<H", boot, 0x16, fat_sectors)
        else:
            struct.pack_into("<H", boot, 0x13, 0)
            struct.pack_into("<I", boot, 0x20, total_sectors)
            struct.pack_into("<I", boot, 0x24, fat_sectors)
            struct.pack_into("<I", boot, 0x2C, root_cluster)
            # FAT32 keeps these at 0x30/0x32; 0x28/0x2A is the FAT12/16 layout.
            struct.pack_into("<H", boot, 0x30, 1)          # FSINFO sector
            struct.pack_into("<H", boot, 0x32, 6)          # backup boot sector
        boot[0x15] = 0xF8                                   # fixed disk
        struct.pack_into("<H", boot, 0x18, 32)              # sectors/track
        struct.pack_into("<H", boot, 0x1A, 64)              # heads
        if kind == "fat16":
            boot[0x26] = 0x29                        # extended BPB signature
            struct.pack_into("<I", boot, 0x27, 0x12345678)
            boot[0x2B:0x36] = b"TESTVOL    "
            boot[0x36:0x3E] = b"FAT16   "
        else:
            boot[0x42] = 0x29
            struct.pack_into("<I", boot, 0x43, 0x12345678)  # volume id
            boot[0x47:0x52] = b"TESTVOL    "
            boot[0x52:0x57] = b"FAT32"
        boot[0x1FE] = 0x55
        boot[0x1FF] = 0xAA
        return bytes(boot)

    boot_bytes = make_boot_sector()
    img[0:SECTOR] = boot_bytes

    if kind == "fat32":
        # FSINFO: free cluster count + next free hint, then a backup boot sector.
        fsinfo = bytearray(SECTOR)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)   # free count unknown
        struct.pack_into("<I", fsinfo, 492, 0xFFFFFFFF)   # next free unknown
        struct.pack_into("<I", fsinfo, 508, 0xAA550000)
        img[SECTOR:SECTOR * 2] = fsinfo
        img[SECTOR * 6:SECTOR * 7] = boot_bytes

    # ---------------------------------------------------------------- FAT
    fat = bytearray(fat_sectors * SECTOR)
    if kind == "fat32":
        struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
        struct.pack_into("<I", fat, 4, 0x0FFFFFFF)
        struct.pack_into("<I", fat, 8, 0x0FFFFFFF)   # root cluster, extended below
    else:
        struct.pack_into("<H", fat, 0, 0xFFF8)
        struct.pack_into("<H", fat, 2, 0xFFFF)

    next_cluster = [3 if kind == "fat32" else 2]
    EOC = 0x0FFFFFFF if kind == "fat32" else 0xFFFF   # end-of-chain marker

    def set_fat(cluster, value):
        if kind == "fat32":
            struct.pack_into("<I", fat, cluster * 4, value)
        else:
            struct.pack_into("<H", fat, cluster * 2, value)

    def alloc(data, first=None):
        """Write `data` into consecutive clusters, chained. Returns the first cluster."""
        if not data:
            return 0
        need = (len(data) + cluster_bytes - 1) // cluster_bytes
        if first is None:
            first = next_cluster[0]
        for i in range(need):
            c = first + i
            set_fat(c, EOC if i == need - 1 else c + 1)
            off = (first_data + (c - 2) * spc) * SECTOR
            chunk = data[i * cluster_bytes:(i + 1) * cluster_bytes]
            img[off:off + len(chunk)] = chunk
        next_cluster[0] = max(next_cluster[0], first + need)
        return first

    # ---------------------------------------------------------------- entries
    def short_entry(name11, attr, cluster, size):
        e = bytearray(32)
        e[0:11] = name11
        e[0x0B] = attr
        struct.pack_into("<H", e, 0x14, (cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", e, 0x1A, cluster & 0xFFFF)
        struct.pack_into("<I", e, 0x1C, size)
        return bytes(e)

    def dot_entry(dots, cluster):
        e = bytearray(32)
        e[0:11] = dots.ljust(11).encode("ascii")
        e[0x0B] = 0x10
        struct.pack_into("<H", e, 0x14, (cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", e, 0x1A, cluster & 0xFFFF)
        return bytes(e)

    def lfn_entries(name, name11):
        """Long-name entries: reverse order, terminated by the short entry."""
        checksum = short_name_checksum(name11)
        pieces = [name[i:i + 13] for i in range(0, len(name), 13)]
        out = []
        for idx in range(len(pieces) - 1, -1, -1):
            e = bytearray(32)
            e[0] = (idx + 1) | (0x40 if idx == len(pieces) - 1 else 0)
            e[0x0B] = 0x0F
            e[0x0D] = checksum
            units = [ord(c) for c in pieces[idx]]
            pad = 0x0000 if len(units) < 13 else None
            while len(units) < 13:
                units.append(pad if pad is not None else 0xFFFF)
                if len(units) == 13:
                    break
            for i in range(min(13, len(units))):
                struct.pack_into("<H", e, DOS_OFFSETS[i], units[i])
            out.append(bytes(e))
        return b"".join(out)

    # ---------------------------------------------------------------- layout
    dirs = {"": bytearray()}
    label11 = b"TESTVOL    "
    dirs[""] += short_entry(label11, 0x08, 0, 0)          # volume label entry

    for rel_path, content, force_lfn in FILES:
        parts = rel_path.split("/")
        d = "/".join(parts[:-1])
        dirs.setdefault(d, bytearray())
        name11 = name83(parts[-1])
        cluster = alloc(content)
        if force_lfn or needs_lfn(parts[-1]):
            dirs[d] += lfn_entries(parts[-1], name11)
        dirs[d] += short_entry(name11, 0x20, cluster, len(content))

    for d in [k for k in dirs if k]:
        name = d.split("/")[-1]
        name11 = name83(name)
        first = next_cluster[0]                            # reserve before writing "."
        body = bytearray()
        body += dot_entry(".", first)
        # Per spec the parent entry is cluster 0 when the parent is the root -
        # not the root cluster number, even on FAT32.
        body += dot_entry("..", 0)
        body += dirs[d]
        alloc(bytes(body), first)
        if needs_lfn(name):
            dirs[""] += lfn_entries(name, name11)
        dirs[""] += short_entry(name11, 0x10, first, 0)

    root_bytes = bytes(dirs[""])
    if kind == "fat32":
        cur = root_cluster
        off = (first_data + (cur - 2) * spc) * SECTOR
        chunk = root_bytes[:cluster_bytes]
        img[off:off + len(chunk)] = chunk
        remaining = root_bytes[cluster_bytes:]
        while remaining:
            nxt = alloc(remaining[:cluster_bytes])
            set_fat(cur, nxt)
            cur = nxt
            remaining = remaining[cluster_bytes:]
        set_fat(cur, EOC)
    else:
        off = (reserved + fat_count * fat_sectors) * SECTOR
        img[off:off + len(root_bytes)] = root_bytes

    for i in range(fat_count):
        off = (reserved + i * fat_sectors) * SECTOR
        img[off:off + len(fat)] = fat

    with open(out_path, "wb") as fh:
        fh.write(img)
    print(f"wrote {out_path} ({kind}, {size_mb} MB, {cluster_count} clusters, "
          f"fat={fat_sectors} sectors, cluster={cluster_bytes} bytes)")


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2], int(sys.argv[3]))
