/* gpuregs2: read-only dump of the 855GM's ring/error registers and a per-32MB count of unmapped aperture pages.  Log: "gpuregs2:" in dmesg. */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>
extern void IOLog(const char *fmt, ...);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);
void __bzero(void *p, unsigned long n) { volatile unsigned char *q = (volatile unsigned char *)p; while (n--) *q++ = 0; }
static inline void outl_(unsigned short port, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(port)); }
static inline unsigned int inl_(unsigned short port) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v; }
static unsigned int pci_rd(unsigned bus, unsigned dev, unsigned fn, unsigned reg)
{ outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc)); return inl_(0xcfc); }

static kern_return_t
gpuregs2_start(kmod_info_t *ki, void *data)
{
	unsigned int bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu, bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu, c, p;
	volatile unsigned int *m = (volatile unsigned int *)ml_io_map(bar1, 0x80000);

	if (!m || !bar0 || !bar1)
		return KERN_SUCCESS;
	IOLog("gpuregs2: HEAD %08x TAIL %08x START %08x CTL %08x\n", m[0x2034 / 4], m[0x2030 / 4], m[0x2038 / 4], m[0x203c / 4]);
	IOLog("gpuregs2: ACTHD %08x IPEIR %08x IPEHR %08x INSTDONE %08x INSTPS %08x INSTPM %08x ISR %08x IIR %08x IMR %08x\n", m[0x2074 / 4], m[0x2088 / 4], m[0x208c / 4], m[0x2090 / 4], m[0x2070 / 4], m[0x20c0 / 4], m[0x20ac / 4], m[0x20a4 / 4], m[0x20a8 / 4]);
	IOLog("gpuregs2: ESR %08x EIR %08x EMR %08x PGTBL_ER %08x PGTBL_CTL %08x D_STATE %08x\n", m[0x20b8 / 4], m[0x20b0 / 4], m[0x20b4 / 4], m[0x2024 / 4], m[0x2020 / 4], m[0x6104 / 4]);
	for (c = 0; c < 4; c++) {
		volatile unsigned int *a = (volatile unsigned int *)ml_io_map(bar0 + c * 0x2000000ul, 0x2000000ul);
		unsigned int un = 0, first = 0, last = 0;

		if (!a) { IOLog("gpuregs2: chunk %u not mapped\n", c); continue; }
		for (p = 0; p < 8192; p++)
			if (a[p * 1024] == 0xf000eec2u) { if (!un) first = p; last = p; un++; }
		IOLog("gpuregs2: aperture %3u..%3u MB: %u unmapped pages (first %u last %u)\n", c * 32, c * 32 + 31, un, first + c * 8192, last + c * 8192);
	}
	return KERN_SUCCESS;
}
static kern_return_t gpuregs2_stop(kmod_info_t *ki, void *data) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(com.local.gpuregs2, "0.1.0", gpuregs2_start, gpuregs2_stop)
