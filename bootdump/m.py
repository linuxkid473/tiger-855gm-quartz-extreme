import mmap, os
# Dump 855GM MMIO ranges via sysfs resource1 (BAR1 = MMIO on gen2). Run as root after i915 init.
f = os.open('/sys/bus/pci/devices/0000:00:02.0/resource1', os.O_RDONLY)
m = mmap.mmap(f, 0x80000, prot=mmap.PROT_READ)
v = memoryview(m).cast('I')
lines = []
for a, n in ((0x2000, 0x400), (0x6000, 0x400), (0x61000, 0x400), (0x70000, 0x2000)):
    for i in range(a // 4, (a + n) // 4):
        lines.append('MMIO 0x%x 0x%08x' % (i * 4, v[i]))
open('/mnt/d/mmio.txt', 'w').write('\n'.join(lines) + '\n')
print('wrote', len(lines), 'registers to /mnt/d/mmio.txt')
