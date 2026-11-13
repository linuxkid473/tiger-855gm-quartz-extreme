/* gttwatch: keeps every UNMAPPED page of the 855GM's 128 MB graphics aperture pointed at one scratch page, for a limited time.
 * (Linux i915 does this for the whole table; Tiger's AppleIntel830 leaves unused entries invalid and PGTBL_ER stays 0x49.)
 * The page table cannot be read back, so unmapped pages are found by reading through the aperture (unmapped = f000eec2).
 * Mapped pages are never written.  A thread rescans every ~15 ms for DURATION_S seconds so buffers the driver allocates (and
 * leaves half unmapped) later, e.g. when WindowServer creates its GL context, are covered too.  Log: "gttwatch:" in dmesg.
 */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>

extern void IOLog(const char *fmt, ...);
extern void IOSleep(unsigned int ms);
extern void *IOMallocContiguous(unsigned long size, unsigned long alignment, unsigned long *phys);
extern void *IOCreateThread(void (*fn)(void *), void *arg);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);

#ifndef DURATION_S
#define DURATION_S 180
#endif

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

#define NCHUNK 4
#define CHUNK_PAGES 8192		/* 32 MB per mapping */
static volatile unsigned int *gAp[NCHUNK];
static volatile unsigned int *gMM;
static volatile int gRunning = 0;
static unsigned int gUnusedTmp;
static unsigned int gPte;

/* Fenced (tiled) regions: the CPU's aperture view is de-tiled before the GTT lookup, so a linear aperture offset does NOT index
 * the page table entry of the same number.  For an X-tiled fence (tile = 512 B x 8 rows = one 4 KB page) with pitch P bytes, the
 * tile at tile-row ty, tile-column tx is GTT page (start/4096 + ty*(P/512) + tx) and its first byte is reached at the aperture
 * offset start + ty*8*P + tx*512. */
/* register values the Linux i915 driver leaves on this 855GM (3D works there); Tiger's AppleIntel830 leaves the BIOS values */
static void
apply_linux_regs(void)
{
	if (gMM[0x20d8 / 4] != 0x011f0108u)
		gMM[0x20d8 / 4] = 0x011f0108u;		/* FW_BLC watermarks */
	if (gMM[0x20dc / 4] != 0x00000102u)
		gMM[0x20dc / 4] = 0x00000102u;		/* FW_BLC2 */
	if (gMM[0x20e4 / 4] != 0x00000002u)
		gMM[0x20e4 / 4] = 0x00000002u;		/* MI_ARB_STATE */
	if (gMM[0x6204 / 4] != 0x00000001u)
		gMM[0x6204 / 4] = 0x00000001u;		/* RENCLK_GATE_D1 */
}

static unsigned int gFStart[8], gFSize[8], gFPitch[8];
static int gNF;

static int
in_fence(unsigned int page)
{
	int f;

	for (f = 0; f < gNF; f++)
		if (page >= gFStart[f] / 4096 && page < (gFStart[f] + gFSize[f]) / 4096)
			return 1;
	return 0;
}

static inline unsigned int
ap_read(unsigned int off)
{
	return gAp[off >> 25][(off & 0x1ffffff) >> 2];
}

static void
watcher(void *arg)
{
	unsigned int pass, p, f, filled_total = 0, logs = 0, erlogs = 0;
	unsigned int passes = DURATION_S * 1000 / 15;

	for (pass = 0; pass < passes; pass++) {
		unsigned int filled = 0, mapped = 0, ftiles = 0;

		for (p = 0; p < NCHUNK * CHUNK_PAGES; p++) {		/* linear (untiled) pages */
			if (in_fence(p))
				continue;
			if (gAp[p / CHUNK_PAGES] == 0)
				continue;
			if (ap_read(p * 4096u) == 0xf000eec2u) {
				gMM[0x10000 / 4 + p] = gPte;
				filled++;
			} else
				mapped++;
		}
		for (f = 0; f < gNF; f++) {				/* X-tiled regions, page by page (gen2 tile = 128 B x 16 rows = 2 KB, two tiles per page) */
			unsigned int pages = gFSize[f] / 4096, ppr = gFPitch[f] / 256, g;	/* pages per tile row = (pitch/128)/2 */

			for (g = 0; g < pages; g++) {
				unsigned int off = gFStart[f] + (g / ppr) * 16 * gFPitch[f] + (g % ppr) * 256;

				if (gAp[off >> 25] == 0)
					continue;
				ftiles++;
				if (ap_read(off) == 0xf000eec2u) {
					gMM[0x10000 / 4 + gFStart[f] / 4096 + g] = gPte;
					filled++;
				} else
					mapped++;
			}
		}
		filled_total += filled;
		if ((pass == 0 || filled) && logs < 150) {
			IOLog("gttwatch: pass %u: %u unmapped -> scratch, %u mapped (%u tiled); PGTBL_ER %08x\n", pass, filled, mapped, ftiles, gMM[0x2024 / 4]);
			logs++;
		}
		{
			unsigned int er = gMM[0x2024 / 4];

			if (er && erlogs < 40) {
				IOLog("gttwatch: pass %u: FRESH PGTBL_ER %08x ESR %08x ACTHD %08x INSTDONE %08x HEAD %08x TAIL %08x\n", pass, er, gMM[0x20b8 / 4], gMM[0x2074 / 4], gMM[0x2090 / 4], gMM[0x2034 / 4], gMM[0x2030 / 4]);
				gMM[0x2024 / 4] = er;
				erlogs++;
			}
		}
		apply_linux_regs();
		IOSleep(15);
	}
	IOLog("gttwatch: finished after %u passes, %u entries written in total\n", pass, filled_total);
	gRunning = 0;
}

static kern_return_t
gttwatch_start(kmod_info_t *ki, void *data)
{
	unsigned int id, bar0, bar1, c;
	unsigned long phys = 0;
	void *scratch;

	id = pci_rd(0, 2, 0, 0);
	bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu;
	bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu;
	IOLog("gttwatch: build " __DATE__ " " __TIME__ " id %08x aperture %08x MMIO %08x\n", id, bar0, bar1);
	if (id != 0x35828086u || bar0 == 0 || bar1 == 0)
		return KERN_SUCCESS;
	gMM = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
	if (gMM == 0)
		return KERN_SUCCESS;
	scratch = IOMallocContiguous(4096, 4096, &phys);
	if (scratch == 0 || phys == 0)
		return KERN_SUCCESS;
	__bzero(scratch, 4096);
	gPte = ((unsigned int)phys & 0xfffff000u) | 1;
	for (c = 0; c < NCHUNK; c++) {
		gAp[c] = (volatile unsigned int *)ml_io_map(bar0 + c * 0x2000000ul, 0x2000000ul);
		if (gAp[c] == 0)
			IOLog("gttwatch: cannot map aperture chunk %u\n", c);
	}
	for (c = 0; c < 8; c++) {
		unsigned int v = gMM[0x2000 / 4 + c];

		if ((v & 1) && !(v & 2)) {			/* valid, X-tiled */
			gFStart[gNF] = v & 0x0ff00000u;
			gFSize[gNF] = 512u * 1024u << ((v >> 8) & 7);
			gFPitch[gNF] = 128u << ((v >> 4) & 7);
			IOLog("gttwatch: fence %u = %08x: start %uMB size %uMB pitch %u X-tiled (gen2 128Bx16 tiles)\n", c, v, gFStart[gNF] >> 20, gFSize[gNF] >> 20, gFPitch[gNF]);
			gNF++;
		} else if (v & 1)
			IOLog("gttwatch: fence %u = %08x: Y-tiled/other, ignored\n", c, v);
	}
	IOLog("gttwatch: PGTBL_CTL %08x PGTBL_ER %08x scratch phys %08lx pte %08x, watching for %d s\n", gMM[0x2020 / 4], gMM[0x2024 / 4], phys, gPte, DURATION_S);
	IOLog("gttwatch: before: FW_BLC %08x FW_BLC2 %08x MI_ARB_STATE %08x RENCLK_GATE_D1 %08x D_STATE %08x\n", gMM[0x20d8 / 4], gMM[0x20dc / 4], gMM[0x20e4 / 4], gMM[0x6204 / 4], gMM[0x6104 / 4]);
	apply_linux_regs();
	IOLog("gttwatch: after:  FW_BLC %08x FW_BLC2 %08x MI_ARB_STATE %08x RENCLK_GATE_D1 %08x\n", gMM[0x20d8 / 4], gMM[0x20dc / 4], gMM[0x20e4 / 4], gMM[0x6204 / 4]);
	gMM[0x2024 / 4] = gMM[0x2024 / 4];	/* write-1-to-clear the sticky page-table error (0x49 since boot) */
	gMM[0x20b8 / 4] = gMM[0x20b8 / 4];
	IOLog("gttwatch: after clearing: PGTBL_ER %08x ESR %08x\n", gMM[0x2024 / 4], gMM[0x20b8 / 4]);
	gRunning = 1;
	IOCreateThread(watcher, 0);
	return KERN_SUCCESS;
}

static kern_return_t
gttwatch_stop(kmod_info_t *ki, void *data)
{
	return gRunning ? KERN_FAILURE : KERN_SUCCESS;
}

KMOD_EXPLICIT_DECL(com.local.gttwatch, "0.1.0", gttwatch_start, gttwatch_stop)
