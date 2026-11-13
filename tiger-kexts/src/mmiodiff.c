/* mmiodiff: print every 855GM MMIO register (ranges Linux's i915 dumped) whose live value differs from the Linux reference.
 * Log lines "mmiodiff:" in dmesg: addr=tiger/linux. Read-only. */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>
#include "mmiodiff_tab.h"
extern void IOLog(const char *fmt, ...);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);
void __bzero(void *p, unsigned long n) { volatile unsigned char *q = (volatile unsigned char *)p; while (n--) *q++ = 0; }
static inline void outl_(unsigned short port, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(port)); }
static inline unsigned int inl_(unsigned short port) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v; }
static unsigned int pci_rd(unsigned bus, unsigned dev, unsigned fn, unsigned reg) { outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc)); return inl_(0xcfc); }
static kern_return_t
mmiodiff_start(kmod_info_t *ki, void *data)
{
	unsigned int bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu, r, i, n = 0, col = 0, tot = 0;
	volatile unsigned int *mm = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
	char line[256];
	if (!mm) { IOLog("mmiodiff: cannot map\n"); return KERN_SUCCESS; }
	IOLog("mmiodiff: start (id %08x)\n", pci_rd(0, 2, 0, 0));
	for (r = 0; r < sizeof(lin_ranges) / sizeof(lin_ranges[0]); r++)
		for (i = 0; i < lin_ranges[r].n; i++) {
			unsigned int a = lin_ranges[r].base + 4 * i, t = mm[a / 4], l = lin_ranges[r].v[i];
			tot++;
			if (t == l) continue;
			n++;
			IOLog("mmiodiff: %05x tiger=%08x linux=%08x\n", a, t, l);
		}
	IOLog("mmiodiff: %u of %u registers differ\n", n, tot);
	return KERN_SUCCESS;
}
static kern_return_t mmiodiff_stop(kmod_info_t *ki, void *data) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(com.local.mmiodiff, "0.1.0", mmiodiff_start, mmiodiff_stop)
