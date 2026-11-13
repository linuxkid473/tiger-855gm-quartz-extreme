/* gpudump: read-only dump of aperture memory (GTT addresses) to the kernel log, "gpudump:" lines.  RANGES = { {gtt_address, dwords}, ... } */
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
static const unsigned int ranges[][2] = { RANGES };
static kern_return_t
gpudump_start(kmod_info_t *ki, void *data)
{
	unsigned int bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu, bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu, r, i;
	volatile unsigned int *m = (volatile unsigned int *)ml_io_map(bar1, 0x80000);

	if (!m || !bar0)
		return KERN_SUCCESS;
	IOLog("gpudump: HEAD %08x TAIL %08x INSTDONE %08x\n", m[0x2034 / 4], m[0x2030 / 4], m[0x2090 / 4]);
	for (r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
		unsigned int base = ranges[r][0] & ~0x1ffffffu;
		volatile unsigned int *a = (volatile unsigned int *)ml_io_map(bar0 + base, 0x2000000);

		if (!a) { IOLog("gpudump: cannot map %08x\n", base); continue; }
		for (i = 0; i < ranges[r][1]; i += 8) {
			volatile unsigned int *p = a + ((ranges[r][0] - base) >> 2) + i;
			unsigned int n = ranges[r][1] - i;

			IOLog("gpudump: %08x: %08x %08x %08x %08x %08x %08x %08x %08x\n", ranges[r][0] + i * 4, p[0], n > 1 ? p[1] : 0, n > 2 ? p[2] : 0, n > 3 ? p[3] : 0, n > 4 ? p[4] : 0, n > 5 ? p[5] : 0, n > 6 ? p[6] : 0, n > 7 ? p[7] : 0);
			if ((i & 63) == 56)
				IOSleep(2);
		}
	}
	return KERN_SUCCESS;
}
static kern_return_t gpudump_stop(kmod_info_t *ki, void *data) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(com.local.gpudump, "0.1.0", gpudump_start, gpudump_stop)
