/* gttfill: look at (probe build) or repair (-DFILL build) the 855GM graphics page table on Tiger.
 *
 * Hypothesis: Tiger's AppleIntel830 leaves unused GTT entries invalid (PGTBL_ER reads 0x49 "invalid GTT PTE" from boot), Linux's
 * i915 points every unused entry at a scratch page (PGTBL_ER 0).  The 3D engine fetches/prefetches more than the 2D engine and
 * stalls on the first fault.  The page table entries cannot be read back (the MMIO window at +0x10000 is write-only, the table's
 * memory is not CPU-readable), so unmapped pages are found by reading through the aperture (unmapped = f000eec2).  The probe build
 * only logs ("gttfill:" in dmesg).  The FILL build points those unmapped pages at one scratch page; mapped pages are never written.
 */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>

extern void IOLog(const char *fmt, ...);
extern void *IOMallocContiguous(unsigned long size, unsigned long alignment, unsigned long *phys);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);

void
__bzero(void *p, unsigned long n)
{
	volatile unsigned char *q = (volatile unsigned char *)p;

	while (n--)
		*q++ = 0;
}

static inline void outl_(unsigned short port, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(port)); }
static inline unsigned int inl_(unsigned short port) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v; }

static unsigned int
pci_rd(unsigned bus, unsigned dev, unsigned fn, unsigned reg)
{
	outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc));
	return inl_(0xcfc);
}

#define MAXUN 40000
static unsigned int unmapped[MAXUN];

static kern_return_t
gttfill_start(kmod_info_t *ki, void *data)
{
	static const unsigned int ranges[][2] = { { 0, 4096 }, { 16384, 20480 } };	/* pages: 0..16MB and 64..80MB of the aperture */
	unsigned int id, bar0, bar1, pctl, per, r, j, ap;
	unsigned int nmapped = 0, nunmapped = 0, nun = 0, nruns = 0, runstart = 0;
	int prev = -1;
	volatile unsigned int *mm;
	volatile unsigned int *gtt;

	id = pci_rd(0, 2, 0, 0);
	bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu;
	bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu;
#ifdef FILL
	IOLog("gttfill: build " __DATE__ " " __TIME__ " FILL\n");
#else
	IOLog("gttfill: build " __DATE__ " " __TIME__ " PROBE\n");
#endif
	IOLog("gttfill: 00:02.0 id %08x aperture %08x MMIO %08x\n", id, bar0, bar1);
	if (id != 0x35828086u || bar0 == 0 || bar1 == 0) {
		IOLog("gttfill: not an 855GM, giving up\n");
		return KERN_SUCCESS;
	}
	mm = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
	if (mm == 0) {
		IOLog("gttfill: cannot map MMIO\n");
		return KERN_SUCCESS;
	}
	pctl = mm[0x2020 / 4];
	per = mm[0x2024 / 4];
	IOLog("gttfill: PGTBL_CTL %08x PGTBL_ER %08x EIR %08x ESR %08x\n", pctl, per, mm[0x20b0 / 4], mm[0x20b8 / 4]);
	gtt = mm + 0x10000 / 4;		/* 830..865: entries are written through the MMIO window at +0x10000 (write-only here) */

	for (r = 0; r < 2; r++) {
		unsigned int np = ranges[r][1] - ranges[r][0];
		volatile unsigned int *apm = (volatile unsigned int *)ml_io_map(bar0 + ranges[r][0] * 4096ul, np * 4096ul);

		if (apm == 0) {
			IOLog("gttfill: cannot map aperture range %u\n", r);
			continue;
		}
		prev = -1;
		for (j = 0; j < np; j++) {
			unsigned int w = apm[j * 1024];
			int v = (w != 0xf000eec2u);	/* 1 = mapped */

			if (j < 3)
				IOLog("gttfill: page %u first dword %08x\n", ranges[r][0] + j, w);
			if (v)
				nmapped++;
			else {
				nunmapped++;
				if (nun < MAXUN)
					unmapped[nun++] = ranges[r][0] + j;
			}
			if (v != prev) {
				if (prev != -1 && nruns < 60)
					IOLog("gttfill: run %s pages %u..%u (%u)\n", prev ? "mapped  " : "UNMAPPED", runstart, ranges[r][0] + j - 1, ranges[r][0] + j - runstart);
				if (prev != -1)
					nruns++;
				runstart = ranges[r][0] + j;
				prev = v;
			}
		}
		if (nruns < 60)
			IOLog("gttfill: run %s pages %u..%u (%u)\n", prev ? "mapped  " : "UNMAPPED", runstart, ranges[r][1] - 1, ranges[r][1] - runstart);
		nruns++;
	}
	IOLog("gttfill: probed pages: %u mapped, %u unmapped, %u runs\n", nmapped, nunmapped, nruns);

#ifdef FILL
	{
		unsigned long phys = 0;
		void *scratch = IOMallocContiguous(4096, 4096, &phys);
		unsigned int pte, i;

		if (scratch == 0 || phys == 0) {
			IOLog("gttfill: no scratch page\n");
			return KERN_SUCCESS;
		}
		__bzero(scratch, 4096);
		pte = ((unsigned int)phys & 0xfffff000u) | 1;
		IOLog("gttfill: scratch page phys %08lx, writing %08x into %u unmapped entries\n", phys, pte, nun);
		for (i = 0; i < nun; i++)
			gtt[unmapped[i]] = pte;
		__asm__ __volatile__("wbinvd" : : : "memory");
		mm[0x2020 / 4] = pctl;			/* re-latch the table (drops cached translations) */
		(void)mm[0x2020 / 4];
		mm[0x2024 / 4] = mm[0x2024 / 4];	/* clear the page-table error bits */
		IOLog("gttfill: done; PGTBL_CTL %08x PGTBL_ER now %08x\n", mm[0x2020 / 4], mm[0x2024 / 4]);
	}
#endif
	return KERN_SUCCESS;
}

static kern_return_t
gttfill_stop(kmod_info_t *ki, void *data)
{
	return KERN_SUCCESS;
}

KMOD_EXPLICIT_DECL(com.local.gttfill, "0.1.0", gttfill_start, gttfill_stop)
