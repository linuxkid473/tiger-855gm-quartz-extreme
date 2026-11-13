#!/usr/bin/env python3
"""Tiny read-only HFS+ extractor (no mount needed).

usage:  hfsx.py IMAGE PART_OFFSET_BYTES ls  /path
        hfsx.py IMAGE PART_OFFSET_BYTES get /path/in/volume  OUTDIR     (file or whole folder, symlinks kept)
"""
import os
import struct
import sys


class HFS:
    def __init__(self, path, off):
        self.f = open(path, "rb")
        self.off = off
        vh = self.read(1024, 512)
        assert vh[:2] in (b"H+", b"HX"), vh[:2]
        self.bs = struct.unpack_from(">I", vh, 40)[0]
        self.total = struct.unpack_from(">I", vh, 44)[0]
        self.ext_fork = self.fork(vh, 192)
        self.cat_fork = self.fork(vh, 272)
        self.cat_data = self.read_fork(self.cat_fork, 0)
        self.ext_data = self.read_fork(self.ext_fork, 0)
        self.cat_leaves = None

    def read(self, pos, n):
        self.f.seek(self.off + pos)
        return self.f.read(n)

    @staticmethod
    def fork(buf, o):
        size = struct.unpack_from(">Q", buf, o)[0]
        total = struct.unpack_from(">I", buf, o + 12)[0]
        ex = []
        for i in range(8):
            s, c = struct.unpack_from(">II", buf, o + 16 + i * 8)
            if c:
                ex.append((s, c))
        return dict(size=size, total=total, ex=ex, fid=None, rsrc=False)

    def extents_overflow(self, fid, rsrc, have_blocks):
        """follow the extents-overflow B-tree for more extents."""
        data = self.ext_data
        nsz = struct.unpack_from(">H", data, 14 + 18)[0]
        first = struct.unpack_from(">I", data, 14 + 10)[0]
        node = first
        out = []
        ft = 0xFF if rsrc else 0
        while node:
            nd = data[node * nsz:(node + 1) * nsz]
            nrec = struct.unpack_from(">H", nd, 10)[0]
            for r in range(nrec):
                ro = struct.unpack_from(">H", nd, nsz - 2 * (r + 1))[0]
                klen, ftype, _pad, kfid, start = struct.unpack_from(">HBBII", nd, ro)
                if kfid == fid and ftype == ft:
                    exs = []
                    for i in range(8):
                        s, c = struct.unpack_from(">II", nd, ro + 2 + klen + i * 8)
                        if c:
                            exs.append((s, c))
                    out.append((start, exs))
            node = struct.unpack_from(">I", nd, 0)[0]
        out.sort()
        res = []
        for _s, exs in out:
            res += exs
        return res

    def read_fork(self, fk, _unused):
        ex = list(fk["ex"])
        have = sum(c for _s, c in ex)
        if have < fk["total"]:
            if fk.get("fid") is None:
                raise RuntimeError("catalog/extents fork needs overflow")
            ex += self.extents_overflow(fk["fid"], fk.get("rsrc", False), have)
        out = bytearray()
        for s, c in ex:
            out += self.read(s * self.bs, c * self.bs)
        return bytes(out[:fk["size"]])

    def leaves(self):
        if self.cat_leaves is not None:
            return self.cat_leaves
        d = self.cat_data
        nsz = struct.unpack_from(">H", d, 14 + 18)[0]
        first = struct.unpack_from(">I", d, 14 + 10)[0]
        recs = []
        node = first
        while node:
            nd = d[node * nsz:(node + 1) * nsz]
            nrec = struct.unpack_from(">H", nd, 10)[0]
            for r in range(nrec):
                ro = struct.unpack_from(">H", nd, nsz - 2 * (r + 1))[0]
                klen = struct.unpack_from(">H", nd, ro)[0]
                parent = struct.unpack_from(">I", nd, ro + 2)[0]
                nlen = struct.unpack_from(">H", nd, ro + 6)[0]
                name = nd[ro + 8:ro + 8 + nlen * 2].decode("utf-16-be")
                rec = nd[ro + 2 + klen + (klen & 1) * 0:]
                recs.append((parent, name, nd[ro + 2 + klen:ro + 2 + klen + 248]))
            node = struct.unpack_from(">I", nd, 0)[0]
        self.cat_leaves = recs
        return recs

    def children(self, cnid):
        out = []
        for parent, name, rec in self.leaves():
            if parent != cnid or not name:
                continue
            t = struct.unpack_from(">H", rec, 0)[0]
            if t == 1:
                out.append((name, "dir", struct.unpack_from(">I", rec, 8)[0], rec))
            elif t == 2:
                out.append((name, "file", struct.unpack_from(">I", rec, 8)[0], rec))
        return out

    def lookup(self, path):
        cnid = 2
        kind = "dir"
        rec = None
        for part in [p for p in path.split("/") if p]:
            for name, k, c, r in self.children(cnid):
                if name == part:
                    cnid, kind, rec = c, k, r
                    break
            else:
                raise FileNotFoundError(path)
        return cnid, kind, rec

    def file_data(self, fid, rec):
        mode = struct.unpack_from(">H", rec, 42)[0]
        fk = self.fork(rec, 88)
        fk["fid"] = fid
        data = self.read_fork(fk, 0)
        return data, mode

    def extract(self, cnid, kind, rec, dest):
        if kind == "dir":
            os.makedirs(dest, exist_ok=True)
            for name, k, c, r in self.children(cnid):
                self.extract(c, k, r, os.path.join(dest, name.replace("/", ":")))
        else:
            data, mode = self.file_data(cnid, rec)
            if (mode & 0o170000) == 0o120000:
                try:
                    os.symlink(data.decode("utf-8", "replace"), dest)
                except FileExistsError:
                    pass
            else:
                with open(dest, "wb") as f:
                    f.write(data)
                os.chmod(dest, mode & 0o7777 or 0o644)


def main():
    img, off, cmd, path = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    h = HFS(img, off)
    cnid, kind, rec = h.lookup(path)
    if cmd == "ls":
        if kind == "dir":
            for name, k, c, r in h.children(cnid):
                print(k, c, name)
        else:
            print("file", cnid)
    elif cmd == "get":
        dest = os.path.join(sys.argv[5], os.path.basename(path.rstrip("/")))
        h.extract(cnid, kind, rec, dest)
        print("extracted to", dest)


if __name__ == "__main__":
    main()
