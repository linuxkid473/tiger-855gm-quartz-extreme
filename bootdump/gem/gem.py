# Run a Mesa-i830-style textured-quad batch on the real GPU through i915 GEM (Linux, as root).
# usage: python3 gem.py b_tri.txt   (batch emitted by emit.c).  Writes a report next to the batch file and prints it.
import sys, os, struct, ctypes, fcntl, time, glob
def IOC(d, nr, size): return (d << 30) | (size << 16) | (0x64 << 8) | (0x40 + nr)
CREATE = IOC(3, 0x1b, 16); PWRITE = IOC(1, 0x1d, 32); PREAD = IOC(1, 0x1c, 32)
EXEC2 = IOC(1, 0x29, 64); WAIT = IOC(3, 0x2c, 16); SETDOM = IOC(1, 0x1f, 12)
fn = sys.argv[1]
batch, relocs, W, H, cp = [], [], 64, 64, 256
TW, TH, TP = 64, 64, 256
for l in open(fn):
    t = l.split()
    if t[0] == 'B': batch.append(int(t[1], 16))
    elif t[0] == 'R': relocs.append((int(t[1]), int(t[2]), int(t[3])))
    elif t[0] == 'P': W, H, cp = int(t[1]), int(t[2]), int(t[3])
    elif t[0] == 'T': TW, TH, TP = int(t[1]), int(t[2]), int(t[3])
out = []
def say(*a):
    s = ' '.join(str(x) for x in a); print(s); out.append(s)
fd = os.open(sorted(glob.glob('/dev/dri/card*'))[0], os.O_RDWR)
def ioc(req, buf):
    buf = bytearray(buf); fcntl.ioctl(fd, req, buf, True); return buf
def create(size):
    return struct.unpack('<QII', ioc(CREATE, struct.pack('<QII', size, 0, 0)))[1]
def addr(b): return ctypes.addressof(ctypes.c_char.from_buffer(b))
def pwrite(h, data):
    b = bytearray(data); ioc(PWRITE, struct.pack('<IIQQQ', h, 0, 0, len(b), addr(b)))
def pread(h, size):
    b = bytearray(size); ioc(PREAD, struct.pack('<IIQQQ', h, 0, 0, size, addr(b))); return b
csize = (cp * H + 4095) & ~4095
hc, ht, hb = create(csize), create((TP * TH + 4095) & ~4095), create(4096)
quad = {(0, 0): 0xffff0000, (1, 0): 0xff00ff00, (0, 1): 0xff0000ff, (1, 1): 0xffffffff}
tex = b''.join(b''.join(struct.pack('<I', quad[(x * 2 // TW, y * 2 // TH)]) for x in range(TW)) + bytes(TP - TW * 4) for y in range(TH))
pwrite(ht, tex)
pwrite(hc, struct.pack('<I', 0xff202020) * (csize // 4))
pwrite(hb, struct.pack('<%dI' % len(batch), *batch))
RE = bytearray()
for idx, bo, delta in relocs:
    RE += struct.pack('<IIQQII', (hc, ht)[bo], delta, idx * 4, 0, (0x2, 0x4)[bo], (0x2, 0)[bo])
reb = (ctypes.c_char * len(RE)).from_buffer(RE)
objs = bytearray()
objs += struct.pack('<IIQQQQQQ', hc, 0, 0, 0, 0, 0, 0, 0)
objs += struct.pack('<IIQQQQQQ', ht, 0, 0, 0, 0, 0, 0, 0)
objs += struct.pack('<IIQQQQQQ', hb, len(relocs), ctypes.addressof(reb), 0, 0, 0, 0, 0)
ob = (ctypes.c_char * len(objs)).from_buffer(objs)
say('batch dwords', len(batch), 'relocs', relocs)
t0 = time.time()
ioc(EXEC2, struct.pack('<QIIIIIIQQQQ', ctypes.addressof(ob), 3, 0, len(batch) * 4, 0, 0, 0, 0, 1, 0, 0))
say('execbuf submitted')
try:
    ioc(WAIT, struct.pack('<IIq', hc, 0, 3000000000)); say('wait ok in %.3fs' % (time.time() - t0))
except OSError as e:
    say('WAIT FAILED', e)
try:
    ioc(SETDOM, struct.pack('<III', hc, 1, 0))
    px = pread(hc, csize)
    def P(x, y): return struct.unpack_from('<I', px, y * cp + x * 4)[0]
    for (x, y, name) in ((W // 4, H // 4, 'TL'), (W * 3 // 4, H // 4, 'TR'), (W // 4, H * 3 // 4, 'BL'), (W * 3 // 4, H * 3 // 4, 'BR'), (1, 1, 'corner')):
        say('pixel', name, '%08x' % P(x, y))
    for y in range(0, H, max(1, H // 16)):
        say(''.join({0xffff0000: 'R', 0xff00ff00: 'G', 0xff0000ff: 'B', 0xffffffff: 'W', 0xff202020: '.'}.get(P(x, y), '?') for x in range(0, W, max(1, W // 32))))
except OSError as e:
    say('READBACK FAILED', e)
for f in glob.glob('/sys/class/drm/card[0-9]/error') + glob.glob('/sys/kernel/debug/dri/0/i915_error_state'):
    try:
        say('--- ' + f); say(open(f).read(4000))
    except Exception as e: say('cannot read', f, e)
open(fn + '.out', 'w').write('\n'.join(out) + '\n')
