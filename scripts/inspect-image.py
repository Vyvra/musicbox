#!/usr/bin/env python3
"""Read files out of a Raspberry Pi OS .img without root, via a minimal FAT32 reader.

The boot partition (p1) is FAT32. Loop-mounting needs root; this doesn't, so the
image can be verified before an SD card is touched.

  inspect-image.py IMG --list-overlays
  inspect-image.py IMG --cat config.txt
  inspect-image.py IMG --find hifiberry
"""
import argparse
import struct
import sys

LFN_ATTR = 0x0F


class Fat32:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.blk = 512
        mbr = self.f.read(512)
        self.part_off = None
        for i in range(4):
            e = mbr[446 + i * 16: 446 + (i + 1) * 16]
            if e[4] == 0x0C or e[4] == 0x0B:  # FAT32 LBA / FAT32
                self.part_off = struct.unpack("<I", e[8:12])[0] * self.blk
                break
        if self.part_off is None:
            raise SystemExit("no FAT32 partition found")

        bs = self._read(self.part_off, 512)
        self.bytes_per_sector = struct.unpack("<H", bs[11:13])[0]
        self.sectors_per_cluster = bs[13]
        self.reserved = struct.unpack("<H", bs[14:16])[0]
        self.num_fats = bs[16]
        self.fat_sectors = struct.unpack("<I", bs[36:40])[0]
        self.root_cluster = struct.unpack("<I", bs[44:48])[0]

        self.cluster_bytes = self.bytes_per_sector * self.sectors_per_cluster
        self.fat_off = self.part_off + self.reserved * self.bytes_per_sector
        data_start = self.reserved + self.num_fats * self.fat_sectors
        # cluster N starts at data_start + (N-2) clusters
        self.data_off = self.part_off + data_start * self.bytes_per_sector

    def _read(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def _next_cluster(self, n):
        e = self._read(self.fat_off + n * 4, 4)
        return struct.unpack("<I", e)[0] & 0x0FFFFFFF

    def _cluster_chain(self, n, limit=100000):
        out = []
        seen = set()
        while 2 <= n < 0x0FFFFFF8 and n not in seen and len(out) < limit:
            seen.add(n)
            out.append(n)
            n = self._next_cluster(n)
        return out

    def _read_chain(self, n):
        return b"".join(self._read(self.data_off + (c - 2) * self.cluster_bytes,
                                  self.cluster_bytes)
                        for c in self._cluster_chain(n))

    def read_dir(self, cluster):
        """Yield (long_name, short_name, attr, first_cluster, size)."""
        raw = self._read_chain(cluster)
        lfn = []
        for i in range(0, len(raw) - 31, 32):
            e = raw[i:i + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5:
                lfn = []
                continue
            attr = e[11]
            if attr == LFN_ATTR:
                seq = e[0] & 0x1F
                chunk = (e[1:11] + e[14:26] + e[28:32]).decode("utf-16-le", "ignore")
                lfn.append((seq, chunk))
                continue
            short = e[0:8].decode("ascii", "ignore").rstrip()
            ext = e[8:11].decode("ascii", "ignore").rstrip()
            short_name = short + ("." + ext if ext else "")
            if lfn:
                name = "".join(c for _, c in sorted(lfn)).split("\x00")[0]
            else:
                name = short_name
            lfn = []
            fc = struct.unpack("<H", e[20:22])[0] << 16 | struct.unpack("<H", e[26:28])[0]
            size = struct.unpack("<I", e[28:32])[0]
            yield name, short_name, attr, fc, size

    def listdir(self, path):
        cluster = self.root_cluster
        for part in [p for p in path.split("/") if p]:
            for name, _, attr, fc, _ in self.read_dir(cluster):
                if name.lower() == part.lower() and attr & 0x10:
                    cluster = fc
                    break
            else:
                raise FileNotFoundError(path)
        return list(self.read_dir(cluster))

    def cat(self, path):
        parent, _, base = path.rpartition("/")
        for name, _, attr, fc, size in self.listdir(parent or "/"):
            if name.lower() == base.lower():
                return self._read_chain(fc)[:size]
        raise FileNotFoundError(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--cat")
    ap.add_argument("--list-overlays", action="store_true")
    ap.add_argument("--ls", default="/")
    ap.add_argument("--find", help="substring to match in the root dir listing")
    a = ap.parse_args()

    fs = Fat32(a.image)
    if a.cat:
        sys.stdout.write(fs.cat(a.cat).decode("utf-8", "replace"))
    elif a.list_overlays:
        names = sorted(n for n, _, at, _, _ in fs.listdir("/overlays") if n.endswith(".dtbo"))
        print(f"{len(names)} overlays; hifiberry ones:")
        for n in names:
            if "hifiberry" in n:
                print(" ", n)
    elif a.find:
        for name, short, attr, fc, size in fs.listdir("/"):
            if a.find.lower() in name.lower():
                print(f"{'DIR' if attr & 0x10 else f'{size:>10}'}  {name}")
    else:
        for name, short, attr, fc, size in fs.listdir(a.ls):
            kind = "DIR" if attr & 0x10 else f"{size:>10}"
            print(f"{kind}  {name}")


if __name__ == "__main__":
    main()
