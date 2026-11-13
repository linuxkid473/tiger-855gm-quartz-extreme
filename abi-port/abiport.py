#!/usr/bin/env python3
"""ABI port of Apple's DTK-era (10.4.1) AppleIntel830.kext to the retail 10.4.8 C++ vtable layout.

Inputs live in work/in (not committed; they are Apple's binaries from the user's own images):
  old/k41.i386  old/mach.sym  old/IO{GraphicsFamily,PCIFamily,NDRVSupport}.i386   (10.4.1 DTK kernel + families)
  new/k48       new/IO*.i386                                                      (10.4.8 kernel + families, from the laptop)
  AppleIntel830.bin                                                                (the DTK accelerator, i386 object)

Stage 3  (this file, `python3 abiport.py vtables`): rewrite all class vtables of the kext into the 10.4.8 layout.
Stage 4  (`python3 abiport.py census`): list virtual-call sites that still use old slot numbers.
"""
import collections, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
IN = os.path.join(HERE, "work", "in")
OUT = os.path.join(HERE, "work", "out")


# ---------------------------------------------------------------- tiny Mach-O (i386) reader
def parse(path):
    d = open(path, "rb").read()
    magic, cpu, sub, ft, ncmds, szc, flags = struct.unpack_from("<IiiIIII", d, 0)
    assert magic == 0xfeedface, hex(magic)
    p = 28; secs = []; symtab = None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, p)
        if cmd == 1:
            nsects = struct.unpack_from("<I", d, p + 48)[0]; q = p + 56
            for _ in range(nsects):
                sn = d[q:q + 16].rstrip(b"\0").decode()
                addr, sz, off, al, reloff, nreloc, fl = struct.unpack_from("<IIIIIII", d, q + 32)
                secs.append(dict(name=sn, addr=addr, size=sz, off=off, reloff=reloff, nreloc=nreloc, hdr=q)); q += 68
        elif cmd == 2:
            symtab = (p,) + struct.unpack_from("<IIII", d, p + 8)
        p += size
    syms = []
    if symtab:
        _, symoff, nsyms, stroff, strsize = symtab
        for i in range(nsyms):
            n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from("<IBBhI", d, symoff + i * 12)
            nm = d[stroff + n_strx:d.index(b"\0", stroff + n_strx)].decode(errors="replace")
            syms.append((nm, n_type, n_sect, n_desc, n_value))
    return dict(d=d, ft=struct.unpack_from("<I", d, 12)[0], secs=secs, syms=syms, symtab=symtab)


def relocs(m, sec):
    d = m["d"]; out = {}
    for i in range(sec["nreloc"]):
        a, info = struct.unpack_from("<II", d, sec["reloff"] + i * 8)
        if a & 0x80000000:
            out[a & 0xffffff] = ("scattered", info); continue
        out[a] = (info & 0xffffff, (info >> 27) & 1, (info >> 28) & 0xf)
    return out


# ---------------------------------------------------------------- kernel vtable layouts (name per slot)
def _otool_secs(img):
    o = subprocess.run(["otool", "-l", img], capture_output=True, text=True).stdout
    return [(n, int(a, 16), int(z, 16), int(off)) for n, s, a, z, off in
            re.findall(r"sectname (\S+)\n\s+segname (\S+)\n\s+addr (0x[0-9a-f]+)\n\s+size (0x[0-9a-f]+)\n\s+offset (\d+)", o)]


def _kernel_layouts(symlist, img):
    a2n = {}; vts = {}
    for n, a in symlist:
        a2n.setdefault(a, n)
        if n.startswith("__ZTV") and not n.startswith("__ZTVN"):
            vts[n[5:]] = a
    secs = _otool_secs(img); data = open(img, "rb").read()

    def rd(a):
        for n, ad, sz, off in secs:
            if ad <= a < ad + sz and off:
                return struct.unpack_from("<I", data, off + a - ad)[0]
    srt = sorted(a2n); lay = {}
    for cls, a in vts.items():
        nxt = next((x for x in srt if x > a), a + 4 * 400)
        lay[cls] = [(a2n.get(rd(a + i * 4)) if rd(a + i * 4) else None) for i in range((nxt - a) // 4)]
    return lay


def kernel_old():
    m = parse(os.path.join(IN, "old/mach.sym"))
    return _kernel_layouts([(n, v) for n, t, s, de, v in m["syms"] if v], os.path.join(IN, "old/k41.i386"))


def kernel_new():
    nm = subprocess.run(["nm", "-n", os.path.join(IN, "new/k48")], capture_output=True, text=True).stdout.splitlines()
    return _kernel_layouts([(l.split()[2], int(l.split()[0], 16)) for l in nm if len(l.split()) == 3],
                           os.path.join(IN, "new/k48"))


def object_layouts(path):
    """vtables of an i386 object-file kext (families, or the accelerator itself): class -> [slot symbol names]."""
    m = parse(path); d = m["d"]; secs = m["secs"]; names = [s[0] for s in m["syms"]]
    bysect = {}
    for n, t, s, de, v in m["syms"]:
        if (t & 0x0e) == 0x0e and s > 0:
            bysect.setdefault((s, v), n)
    out = {}
    for si, sec in enumerate(secs):
        if sec["name"] not in ("__const", "__data"):
            continue
        rel = relocs(m, sec)
        vts = sorted([(v, n) for n, t, s, de, v in m["syms"] if n.startswith("__ZTV") and s == si + 1])
        for a, n in vts:
            nxt = next((x[0] for x in sorted([(v, nn) for nn, t, s, de, v in m["syms"] if s == si + 1 and v > a])),
                       sec["addr"] + sec["size"])
            slots = []
            for i in range((nxt - a) // 4):
                off = a - sec["addr"] + i * 4
                val = struct.unpack_from("<I", d, sec["off"] + off)[0]
                r = rel.get(off)
                if r and r[0] != "scattered":
                    slots.append((names[r[0]] + ("" if val == 0 else "+%d" % val)) if r[1]
                                 else bysect.get((r[0], val), "local_%d_%x" % (r[0], val)))
                else:
                    slots.append(("raw_%x" % val) if val else None)
            out[n[5:]] = slots
    return out


def trimmed(sl):
    n = len(sl)
    while n > 0 and sl[n - 1] is None:
        n -= 1
    return sl[:n]


# ---------------------------------------------------------------- the transform
HIER = {"14Intel830Memory": ("8OSObject", "kern"), "16IOIntel830Shared": ("8OSObject", "kern"),
        "17IOIntel830Surface": ("12IOUserClient", "kern"), "19IOIntel8302DContext": ("12IOUserClient", "kern"),
        "19IOIntel830GLContext": ("12IOUserClient", "kern"), "21IOIntel830Accelerator": ("13IOAccelerator", "fam"),
        "8Intel830": ("21IOIntel830Accelerator", "local"), "17Intel830GLContext": ("19IOIntel830GLContext", "local"),
        "17Intel8302DContext": ("19IOIntel8302DContext", "local"), "15Intel830Surface": ("17IOIntel830Surface", "local")}
ORDER = list(HIER)
for _k in list(HIER):
    HIER["N" + _k + "9MetaClassE"] = ("11OSMetaClass", "kern")
ORDER += ["N" + k + "9MetaClassE" for k in list(ORDER)]


class Model:
    def __init__(self):
        self.okern = kernel_old(); self.nkern = kernel_new()
        self.fold = {}; self.fnew = {}
        for k in ("IOGraphicsFamily", "IOPCIFamily", "IONDRVSupport"):
            self.fold.update(object_layouts(os.path.join(IN, "old", k + ".i386")))
            self.fnew.update(object_layouts(os.path.join(IN, "new", k + ".i386")))
        self.kpath = os.path.join(IN, "AppleIntel830.bin")
        self.kext = object_layouts(self.kpath)
        self.ksyms = set(s[0] for s in parse(self.kpath)["syms"])
        self.O2N = {}; self.NEW = {}; self.REPORT = {}
        for K in ORDER:
            self._run(K)

    @staticmethod
    def align(oldS, newS):
        pos = collections.defaultdict(list)
        for j, n in enumerate(newS):
            if n and j >= 2:
                pos[n].append(j)
        seen = collections.Counter(); o2n = {}
        for i, n in enumerate(oldS):
            if i < 2 or not n:
                continue
            k = seen[n]; seen[n] += 1
            o2n[i] = pos[n][k] if n in pos and k < len(pos[n]) else None
        o2n[2] = 3                                   # old single (deleting) destructor slot -> new D0 slot
        used = set(v for v in o2n.values() if v is not None)
        for i, n in enumerate(oldS):                 # functions whose defining class changed (e.g. IOUserClient::init)
            if i >= 3 and n and o2n.get(i) is None and "RESERVED" not in n:
                j = (o2n.get(i - 1) or 0) + 1
                if j < len(newS) and j not in used:
                    o2n[i] = j; used.add(j)
        return o2n

    def _run(self, K):
        sup, kind = HIER[K]; oldK = trimmed(self.kext[K])
        if kind == "local":
            oldS = trimmed(self.kext[sup]); newS = self.NEW[sup]; o2n = dict(self.O2N[sup])
        else:
            oldS = trimmed((self.okern if kind == "kern" else self.fold)[sup])
            newS = trimmed((self.nkern if kind == "kern" else self.fnew)[sup]); o2n = self.align(oldS, newS)
        new = list(newS); over = 0; unmapped = []
        for i in range(3, len(oldS)):
            if i < len(oldK) and oldK[i] != oldS[i]:
                j = o2n.get(i)
                if j is None:
                    unmapped.append((i, oldS[i], oldK[i]))
                else:
                    new[j] = oldK[i]; over += 1
        new[3] = oldK[2]
        d1 = "__ZN%sD1Ev" % K; d2 = "__ZN%sD2Ev" % K
        new[2] = d1 if d1 in self.ksyms else (d2 if d2 in self.ksyms else newS[2])
        if K.startswith("N") and K.endswith("9MetaClassE"):
            new[2] = "__ZN" + K[1:-1] + "D0Ev"       # metaclass objects are never destroyed; avoid an unexported symbol
        own = oldK[len(oldS):]
        for k in range(len(own)):
            o2n[len(oldS) + k] = len(newS) + k
        self.O2N[K] = o2n; self.NEW[K] = new + own
        self.REPORT[K] = dict(old=len(oldK), new=len(new) + len(own), over=over, own=len(own), unmapped=unmapped)


# ---------------------------------------------------------------- Stage 3: write the rewritten vtables
def write_vtables(model, dst):
    m = parse(model.kpath); d = bytearray(m["d"]); secs = m["secs"]; syms = m["syms"]
    ci = [i for i, s in enumerate(secs) if s["name"] == "__const"][0]; const = secs[ci]; CS = ci + 1
    TS = [i for i, s in enumerate(secs) if s["name"] == "__text"][0] + 1
    _, symoff, nsyms, stroff, strsize = m["symtab"]; symcmd = m["symtab"][0]
    assert stroff + strsize == len(d) and symoff + nsyms * 12 == stroff
    reloff, nreloc = struct.unpack_from("<II", d, const["hdr"] + 48)
    first = {}
    for i, (n, t, s, de, v) in enumerate(syms):
        first.setdefault(n, i)
    newsyms = []

    def ref(name):
        add = 0
        if "+" in name and not name.startswith("raw"):
            base, _, a = name.rpartition("+")
            if a.isdigit() and base in first:
                name, add = base, int(a)
        if name in first:
            i = first[name]; n, t, s, de, v = syms[i]
            if (t & 0x0e) == 0x0e and s == TS:
                return ("L", TS, v, add)
            if (t & 0x0e) == 0x0e:
                raise SystemExit("defined in another section: " + name)
            return ("E", i, add)
        if name not in newsyms:
            newsyms.append(name)
        return ("E", nsyms + newsyms.index(name), add)
    allv = sorted(set(v for n, t, s, de, v in syms if s == CS))
    regions = {}
    for K in ORDER:
        vs = [v for n, t, s, de, v in syms if n == "__ZTV" + K and s == CS][0]
        nxt = next((x for x in allv if x > vs), const["addr"] + const["size"])
        regions[K] = (vs - const["addr"], (nxt - vs) // 4)
    old_rel = [struct.unpack_from("<II", d, reloff + i * 8) for i in range(nreloc)]
    inreg = lambda a: any(o <= a < o + n * 4 for o, n in regions.values())
    keep = [r for r in old_rel if (r[0] & 0x80000000) or not inreg(r[0])]
    assert not [r for r in old_rel if (r[0] & 0x80000000) and inreg(r[0] & 0xffffff)]
    new_rel = []
    for K in ORDER:
        off, n = regions[K]; lst = model.NEW[K]; assert len(lst) <= n, (K, len(lst), n)
        base = const["off"] + off
        for i in range(n):
            struct.pack_into("<I", d, base + i * 4, 0)
        for i, nm in enumerate(lst):
            if i < 2 or nm is None:
                continue
            r = ref(nm)
            if r[0] == "L":
                struct.pack_into("<I", d, base + i * 4, r[2] + r[3]); info = r[1] | (2 << 25)
            else:
                struct.pack_into("<I", d, base + i * 4, r[2]); info = r[1] | (2 << 25) | (1 << 27)
            new_rel.append((off + i * 4, info))
    allrel = sorted(keep + new_rel, key=lambda r: -(r[0] & 0xffffff)); assert len(allrel) <= nreloc
    for i, (a, inf) in enumerate(allrel):
        struct.pack_into("<II", d, reloff + i * 8, a, inf)
    for i in range(len(allrel), nreloc):
        struct.pack_into("<II", d, reloff + i * 8, 0, 0)
    struct.pack_into("<I", d, const["hdr"] + 52, len(allrel))
    refd = set()
    for si, sec in enumerate(secs):
        ro, nr = struct.unpack_from("<II", d, sec["hdr"] + 48)
        for k in range(nr):
            ra, inf = struct.unpack_from("<II", d, ro + k * 8)
            if not (ra & 0x80000000) and (inf >> 27) & 1:
                refd.add(inf & 0xffffff)
    neutral = 0
    for i, (n, t, s, de, v) in enumerate(syms):
        if (t & 0x0e) == 0 and s == 0 and i not in refd:
            struct.pack_into("<IBBhI", d, symoff + i * 12, struct.unpack_from("<I", d, symoff + i * 12)[0], 0x02, 0, 0, 0)
            neutral += 1
    nl = b""; extra = b""
    for nm in newsyms:
        nl += struct.pack("<IBBhI", strsize + len(extra), 0x01, 0, 0, 0); extra += nm.encode() + b"\0"
    tail = bytes(d[symoff:symoff + nsyms * 12]) + nl + bytes(d[stroff:stroff + strsize]) + extra
    d = d[:symoff] + bytearray(tail)
    struct.pack_into("<IIII", d, symcmd + 8, symoff, nsyms + len(newsyms), symoff + (nsyms + len(newsyms)) * 12,
                     strsize + len(extra))
    open(dst, "wb").write(d)
    return dict(size=len(d), relocs=(nreloc, len(allrel)), new_undefined=newsyms, neutralised=neutral)


def verify(model, path):
    got = object_layouts(path); bad = 0; total = 0
    for K in ORDER:
        w = model.NEW[K]; g = got[K]
        for i in range(2, len(w)):
            total += 1; bad += (w[i] != g[i])
    return total, bad


# ---------------------------------------------------------------- Stage 4: patch virtual-call slot numbers
def text_sites(path):
    """linear-sweep the __text section with ndisasm; return (text section dict, instruction list, call/jmp [reg+disp] sites)."""
    m = parse(path); t = [s for s in m["secs"] if s["name"] == "__text"][0]
    raw = bytes(m["d"][t["off"]:t["off"] + t["size"]]); tmp = os.path.join(OUT, "text.bin"); open(tmp, "wb").write(raw)
    out = subprocess.run(["ndisasm", "-b", "32", tmp], capture_output=True, text=True).stdout.splitlines()
    ins = []
    for l in out:
        p = l.split(None, 2)
        if len(p) >= 3:
            ins.append((int(p[0], 16), p[1], p[2]))
    pat = re.compile(r"^(call|jmp)\s+(?:dword\s+)?(?:near\s+)?\[(e[a-z]{2})(?:\+(0x[0-9a-f]+))?\]$")
    sites = []
    for a, hx, tx in ins:
        mm = pat.match(tx)
        if mm and mm.group(2) not in ("esp", "ebp") and (int(mm.group(3), 16) if mm.group(3) else 0) % 4 == 0:
            sites.append((a, mm.group(1), mm.group(2), int(mm.group(3), 16) if mm.group(3) else 0, len(hx) // 2, hx))
    return m, t, ins, sites


def slot_map(model):
    """global old slot -> new slot, majority vote over every kernel/family/local class that has a function there."""
    votes = collections.defaultdict(collections.Counter)
    for lay_old, lay_new in ((model.okern, model.nkern), (model.fold, model.fnew)):
        for c, l in lay_old.items():
            if c in lay_new:
                for i, j in model.align(trimmed(l), trimmed(lay_new[c])).items():
                    if j is not None:
                        votes[i][j] += 1
    for K, o2n in model.O2N.items():
        for i, j in o2n.items():
            if j is not None:
                votes[i][j] += 1
    return {i: c.most_common(1)[0][0] for i, c in votes.items()}, votes


def patch_calls(model, src, dst):
    m, t, ins, sites = text_sites(src); d = bytearray(m["d"])
    f, votes = slot_map(model); log = []; uncovered = []
    for a, kind, reg, disp, n, hx in sites:
        i = disp // 4
        if i not in f:
            uncovered.append((a, i)); continue
        j = f[i]; nd = j * 4
        modrm = d[t["off"] + a + 1]; mod = modrm >> 6
        if mod == 1:
            assert nd < 128, (hex(a), i, j)
            d[t["off"] + a + 2] = nd
        elif mod == 2:
            struct.pack_into("<I", d, t["off"] + a + 2, nd)
        else:
            raise SystemExit("unexpected modrm at %#x" % a)
        log.append((a, i, j, len(votes[i])))
    open(dst, "wb").write(d)
    return log, uncovered, f, votes


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "vtables"
    os.makedirs(OUT, exist_ok=True)
    model = Model()
    if cmd == "vtables":
        dst = os.path.join(OUT, "AppleIntel830.vt")
        info = write_vtables(model, dst); total, bad = verify(model, dst)
        print("wrote", dst, info); print("round-trip: %d slots, %d mismatches" % (total, bad))
    elif cmd == "calls":
        vt = os.path.join(OUT, "AppleIntel830.vt"); dst = os.path.join(OUT, "AppleIntel830.new")
        log, unc, f, votes = patch_calls(model, vt, dst)
        changed = sum(1 for a, i, j, nv in log if i != j)
        print("patched %d virtual-call sites (%d changed slot), uncovered: %s" % (len(log), changed, unc))
        amb = [(i, dict(votes[i])) for a, i, j, nv in log if nv > 1]
        print("sites whose slot had competing votes:", len(amb), sorted(set((i, tuple(sorted(v.items()))) for i, v in amb))[:6])
        open(os.path.join(OUT, "calls.log"), "w").write("\n".join("%#x old_slot %d -> new_slot %d" % x[:3] for x in log))
        print("wrote", dst)
    else:
        raise SystemExit("unknown command " + cmd)


if __name__ == "__main__":
    main()


# ---------------------------------------------------------------- Stage 4b: qualified calls (Parent::fn()) through fixed vtable addresses
def qualified_call_sites(path):
    """`call [abs]` (FF 15) sites whose absolute address points into a vtable: extern '__ZTV...+off' or local/scattered."""
    m = parse(path); d = m["d"]; secs = m["secs"]
    t = [s for s in secs if s["name"] == "__text"][0]; ci = [i for i, s in enumerate(secs) if s["name"] == "__const"][0]
    const = secs[ci]; CS = ci + 1; names = [s[0] for s in m["syms"]]
    vts = sorted([(v, n) for n, ty, s, de, v in m["syms"] if n.startswith("__ZTV") and s == CS])
    allv = sorted(set(v for n, ty, s, de, v in m["syms"] if s == CS))
    reg = [(v, next((x for x in allv if x > v), const["addr"] + const["size"]), n[5:]) for v, n in vts]
    raw = [struct.unpack_from("<II", d, t["reloff"] + i * 8) for i in range(t["nreloc"])]
    out = []
    for r0, r1 in raw:
        scat = bool(r0 & 0x80000000); a = (r0 & 0xffffff) if scat else r0
        if bytes(d[t["off"] + a - 2:t["off"] + a]) != b"\xff\x15":
            continue
        val = struct.unpack_from("<I", d, t["off"] + a)[0]
        if scat:
            hit = [(lo, n) for lo, hi, n in reg if lo == r1]
            if hit:
                out.append((a, "local", hit[0][1], (val - r1) // 4))
        else:
            symnum = r1 & 0xffffff; ext = (r1 >> 27) & 1
            if ext and names[symnum].startswith("__ZTV"):
                out.append((a, "extern", names[symnum][5:], val // 4))
            elif not ext and symnum == CS:
                hit = [(lo, n) for lo, hi, n in reg if lo <= val < hi]
                if hit:
                    out.append((a, "local", hit[0][1], (val - hit[0][0]) // 4))
    return t, out


def patch_qualified(model, src, dst, orig):
    m = parse(src); d = bytearray(m["d"]); t, sites = qualified_call_sites(orig); log = []
    for a, kind, tbl, i in sites:
        if kind == "extern":
            if tbl in model.okern and tbl in model.nkern:
                oldl, newl = trimmed(model.okern[tbl]), trimmed(model.nkern[tbl])
            elif tbl in model.fold and tbl in model.fnew:
                oldl, newl = trimmed(model.fold[tbl]), trimmed(model.fnew[tbl])
            else:
                raise SystemExit("no layouts for " + tbl)
            o2n = model.align(oldl, newl); j = o2n.get(i)
            oname = oldl[i] if i < len(oldl) else None; nname = newl[j] if j is not None and j < len(newl) else None
        else:
            o2n = model.O2N[tbl]; j = o2n.get(i)
            oname = trimmed(model.kext[tbl])[i] if i < len(trimmed(model.kext[tbl])) else None
            nname = model.NEW[tbl][j] if j is not None and j < len(model.NEW[tbl]) else None
        log.append((a, kind, tbl, i, j, oname, nname))
        if j is None:
            continue
        cur = struct.unpack_from("<I", d, t["off"] + a)[0]
        delta = (j - i) * 4
        struct.pack_into("<I", d, t["off"] + a, cur + delta)
    open(dst, "wb").write(d)
    return log
