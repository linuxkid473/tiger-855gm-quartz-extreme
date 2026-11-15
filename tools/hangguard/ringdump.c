/* ringdump: read-only. Dumps the tail of the 855GM command ring and the batch buffers it points to ("ringdump:" log lines). */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>
extern void IOLog(const char *fmt, ...);
extern void IOSleep(unsigned int ms);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);
void __bzero(void *p, unsigned long n) { volatile unsigned char *q = (volatile unsigned char *)p; while (n--) *q++ = 0; }
static inline void outl_(unsigned short port, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(port)); }
static inline unsigned int inl_(unsigned short port) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v; }
static unsigned int pci_rd(unsigned bus, unsigned dev, unsigned fn, unsigned reg)
{ outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc)); return inl_(0xcfc); }

static unsigned int bar0;
static volatile unsigned int *amap[4];
static volatile unsigned int *aper(unsigned int gtt)
{
    unsigned int c = gtt >> 25;
    if (c > 3) return 0;
    if (!amap[c]) amap[c] = (volatile unsigned int *)ml_io_map(bar0 + c * 0x2000000ul, 0x2000000ul);
    return amap[c];
}
static unsigned int rd(unsigned int gtt)
{
    volatile unsigned int *a = aper(gtt);
    return a ? a[(gtt & 0x1ffffff) >> 2] : 0xdeadbeef;
}
static void dump(const char *tag, unsigned int gtt, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i += 8) {
        IOLog("ringdump: %s %08x: %08x %08x %08x %08x %08x %08x %08x %08x\n", tag, gtt + i * 4,
              rd(gtt + i * 4), rd(gtt + i * 4 + 4), rd(gtt + i * 4 + 8), rd(gtt + i * 4 + 12),
              rd(gtt + i * 4 + 16), rd(gtt + i * 4 + 20), rd(gtt + i * 4 + 24), rd(gtt + i * 4 + 28));
        if ((i & 63) == 56) IOSleep(2);
    }
}

static kern_return_t ringdump_start(kmod_info_t *ki, void *data)
{
    unsigned int bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu, i, start, ctl, size, head, tail, nb = 0;
    volatile unsigned int *m;

    bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu;
    m = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
    if (!m || !bar0) return KERN_SUCCESS;
    start = m[0x2038 / 4] & 0xfffff000u; ctl = m[0x203c / 4];
    size = (((ctl >> 12) & 0x1ff) + 1) * 4096;
    head = m[0x2034 / 4] & 0x1ffffcu; tail = m[0x2030 / 4] & 0x1ffffcu;
    IOLog("ringdump: START %08x CTL %08x size %u HEAD %06x TAIL %06x\n", start, ctl, size, head, tail);
    {   /* dump raw surface memory: colour (0x04001000) and depth (0x04001400) first rows */
        unsigned int base, r;
        unsigned int bases[3] = { 0x04001000, 0x04001400, 0x04001800 };
        for (base = 0; base < 3; base++) {
            for (r = 0; r < 4; r++)
                IOLog("ringdump: surf %08x+%03x: %08x %08x %08x %08x %08x %08x %08x %08x\n", bases[base], r * 0x20, rd(bases[base] + r*0x20), rd(bases[base] + r*0x20 + 4), rd(bases[base] + r*0x20 + 8), rd(bases[base] + r*0x20 + 12), rd(bases[base] + r*0x20 + 16), rd(bases[base] + r*0x20 + 20), rd(bases[base] + r*0x20 + 24), rd(bases[base] + r*0x20 + 28));
            IOLog("ringdump: surf %08x+800: %08x %08x %08x %08x\n", bases[base], rd(bases[base]+0x800), rd(bases[base]+0x804), rd(bases[base]+0x808), rd(bases[base]+0x80c));
        }
    }
    return KERN_SUCCESS;
}
static kern_return_t ringdump_stop(kmod_info_t *ki, void *data) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(com.local.ringdump, "0.1.0", ringdump_start, ringdump_stop)
