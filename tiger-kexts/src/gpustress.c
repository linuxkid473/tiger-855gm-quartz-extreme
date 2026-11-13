/* gpustress: does the 855GM stall on Tiger when fed a KNOWN-GOOD (Linux-proven) textured batch through Apple's own ring path?
 *
 * Needs the p45 AppleIntel830 (stores 'this' and &submit_buffer in a slot block tagged GSM1GSM2).  Maps 9 pages of our own memory
 * into free GTT slots (dst 64x64 linear, texture 64x64, batch), then submits IGT's gen2 textured copy NLOOPS times through
 * Intel830::submit_buffer and watches ring HEAD/TAIL.  If HEAD stops with HEAD != TAIL for ~3 s the GPU stalled: log registers.
 * Log: "gpustress:" in dmesg.
 */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>

#ifndef VARIANT
#define VARIANT 2
#endif
#include "gpustress_batch.h"

extern void IOLog(const char *fmt, ...);
extern void IOSleep(unsigned int ms);
extern void *IOMallocContiguous(unsigned long size, unsigned long alignment, unsigned long *phys);
extern void *IOCreateThread(void (*fn)(void *), void *arg);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);

#ifndef VARIANT
#define VARIANT 2
#endif
#ifndef NLOOPS
#define NLOOPS 4000
#endif
#ifndef PER_BURST
#define PER_BURST 8
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

static void
pci_wr(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned int v)
{
	outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc));
	outl_(0xcfc, v);
}

/* GTT translation cache flush: host bridge 00:00.0 AGPCTRL (0xb0) bit 7 = GTLB enable; clear then set (Linux intel-agp / Apple do this) */
static void
gtt_tlb_flush(void)
{
	unsigned int v = pci_rd(0, 0, 0, 0xb0);

	pci_wr(0, 0, 0, 0xb0, v & ~0x80u);
	(void)pci_rd(0, 0, 0, 0xb0);
	pci_wr(0, 0, 0, 0xb0, v | 0x80u);
	(void)pci_rd(0, 0, 0, 0xb0);
}

typedef int (*submit_fn)(void *self, unsigned long *buf, unsigned long gtt, unsigned long n, char *file, int line);

static volatile unsigned int *gMM;
static volatile unsigned int *gAp;
static unsigned int gBase;			/* first GTT page of our block */
static unsigned int *gBuf;			/* kernel virtual of our 9 pages */
static unsigned long gPhys;
static kmod_info_t *gKi;
static volatile int gRunning;
static unsigned int gTexG;

#define PG_DST 0
#define PG_TEX 4
#define PG_BAT 8

static int
find_slots(void **self, submit_fn *fn)
{
	kmod_info_t *k;
	unsigned char *p, *e;

	for (k = gKi->next; k; k = k->next) {
		const char *n = k->name, *w = "com.apple.driver.AppleIntel830";
		while (*n && *n == *w) { n++; w++; }
		if (*n || *w)
			continue;
		p = (unsigned char *)k->address;
		e = p + k->size - 16;
		for (; p < e; p += 4) {
			if (*(unsigned int *)p == 0x314d5347u && *(unsigned int *)(p + 4) == 0x324d5347u) {
				*self = (void *)*(unsigned int *)(p + 8);
				*fn = (submit_fn)*(unsigned int *)(p + 12);
				IOLog("gpustress: AppleIntel830 at %08lx size %lx, slot block at %08lx: this=%p fn=%p\n", (unsigned long)k->address, (unsigned long)k->size, (unsigned long)p, *self, (void *)*fn);
				return *self != 0 && *fn != 0;
			}
		}
		IOLog("gpustress: AppleIntel830 found but no GSM slot block (not p45?)\n");
		return 0;
	}
	IOLog("gpustress: AppleIntel830 not in kmod list\n");
	return 0;
}

static void
regs(const char *tag)
{
	IOLog("gpustress: %s HEAD %08x TAIL %08x ACTHD %08x IPEHR %08x INSTDONE %08x EIR %08x ESR %08x PGTBL_ER %08x\n", tag, gMM[0x2034 / 4], gMM[0x2030 / 4],
	    gMM[0x2074 / 4], gMM[0x2068 / 4], gMM[0x2090 / 4], gMM[0x20b0 / 4], gMM[0x20b8 / 4], gMM[0x2024 / 4]);
}

static void
worker(void *arg)
{
	void *self = 0;
	submit_fn fn = 0;
	unsigned int i, w, lastHead = 0xffffffffu, same = 0, ok = 0;
	unsigned int dstG, texG, batG;
	unsigned long *bat = (unsigned long *)((unsigned char *)gBuf + PG_BAT * 4096);

	for (i = 0; i < 600 && !find_slots(&self, &fn); i++)	/* wait up to ~60 s for Apple's driver to submit something */
		IOSleep(100);
	if (!self || !fn) {
		IOLog("gpustress: giving up, no this/fn\n");
		gRunning = 0;
		return;
	}
	dstG = (gBase + PG_DST) * 4096u;
	texG = (gBase + PG_TEX) * 4096u;
	batG = (gBase + PG_BAT) * 4096u;
	for (i = 0; i < BATCH_N; i++)
		bat[i] = batch_tmpl[i];
	bat[BATCH_N] = 0x05000000;	/* submit_buffer writes this too, but through the CPU cache: pre-place it so memory already has it */
#ifdef GS_APPLE
	dstG = 0x04000000u;
	texG = gTexG | TEX_FLAGS;
#endif
	if (DST_IDX >= 0)
		bat[DST_IDX] = dstG;
	if (TEX_IDX >= 0)
		bat[TEX_IDX] = texG;
	__asm__ __volatile__("wbinvd" : : : "memory");
	regs("start");
	IOLog("gpustress: stamps next %u cached-lastread %u; submitting %d batches (dst %08x tex %08x batch %08x)\n", *(unsigned int *)((char *)self + 0x50), *(unsigned int *)((char *)self + 0x54), NLOOPS, dstG, texG, batG);
	for (i = 0; i < NLOOPS; i++) {
		fn(self, bat, batG, BATCH_N, "gpustress", 1);
		ok++;
		if ((i % PER_BURST) == PER_BURST - 1) {
			IOSleep(1);
			w = gMM[0x2034 / 4] & 0x1ffffcu;
			if (w == lastHead && (gMM[0x2034 / 4] & 0x1ffffcu) != (gMM[0x2030 / 4] & 0x1ffffcu)) {
				if (++same > 300) {
					IOLog("gpustress: STALL after %u submits\n", ok);
					regs("stall");
					break;
				}
			} else
				same = 0;
			lastHead = w;
		}
		if ((i % 2000) == 1999) {
			IOLog("gpustress: %u submits ok, stamps next %u cached-lastread %u\n", ok, *(unsigned int *)((char *)self + 0x50), *(unsigned int *)((char *)self + 0x54));
			regs("progress");
		}
	}
	IOSleep(200);
	__asm__ __volatile__("wbinvd" : : : "memory");
	regs("end");
	{
		unsigned int *d = gBuf;
		IOLog("gpustress: result dst pixels (16,16)=%08x (48,16)=%08x (16,48)=%08x (48,48)=%08x (expect 4 colours if the GPU drew)\n",
		    d[16 * 64 + 16], d[16 * 64 + 48], d[48 * 64 + 16], d[48 * 64 + 48]);
	}
	IOLog("gpustress: finished, %u submits\n", ok);
	gRunning = 0;
}

static kern_return_t
gpustress_start(kmod_info_t *ki, void *data)
{
	unsigned int id, bar0, bar1, pctl, cand, j, i;
	static const unsigned int bases[] = { 10240, 11264, 12288, 13312 };	/* 40, 44, 48, 52 MB: below the 64 MB fence */
	volatile unsigned int *ap;
	unsigned int *tex;

	gKi = ki;
	id = pci_rd(0, 2, 0, 0);
	bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu;
	bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu;
	IOLog("gpustress: variant %d build " __DATE__ " " __TIME__ " id %08x aperture %08x MMIO %08x\n", VARIANT, id, bar0, bar1);
	if (id != 0x35828086u || !bar0 || !bar1)
		return KERN_SUCCESS;
	gMM = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
	if (!gMM)
		return KERN_SUCCESS;
	gBuf = (unsigned int *)IOMallocContiguous(9 * 4096, 4096, &gPhys);
	if (!gBuf || !gPhys) {
		IOLog("gpustress: no memory\n");
		return KERN_SUCCESS;
	}
	for (j = 0; j < 9 * 1024; j++)
		gBuf[j] = 0;
	/* pick 9 unmapped aperture pages */
	gBase = 0;
	for (cand = 0; cand < 4 && !gBase; cand++) {
		int bad = 0;

		ap = (volatile unsigned int *)ml_io_map(bar0 + bases[cand] * 4096ul, 9 * 4096ul);
		if (!ap)
			continue;
		for (j = 0; j < 9; j++)
			if (ap[j * 1024] != 0xf000eec2u && ap[j * 1024] != 0xfeedbeefu)
				bad = 1;
		if (!bad) {
			gBase = bases[cand];
			gAp = ap;
		}
	}
	if (!gBase) {
		IOLog("gpustress: no free GTT window found\n");
		return KERN_SUCCESS;
	}
	/* texture: 64x64, four colours; dst: grey */
	tex = gBuf + PG_TEX * 1024;
	for (j = 0; j < 64; j++)
		for (i = 0; i < 64; i++)
			tex[j * 64 + i] = (i < 32) ? (j < 32 ? 0xffff0000u : 0xff0000ffu) : (j < 32 ? 0xff00ff00u : 0xffffffffu);
	for (j = 0; j < 4 * 1024; j++)
		gBuf[PG_DST * 1024 + j] = 0xff202020u;
	for (j = 0; j < 9; j++)
		gMM[0x10000 / 4 + gBase + j] = ((unsigned int)gPhys + j * 4096u) | 1;
	__asm__ __volatile__("wbinvd" : : : "memory");
	pctl = gMM[0x2020 / 4];
	gMM[0x2020 / 4] = pctl;
	(void)gMM[0x2020 / 4];
	IOLog("gpustress: pci 00:02.0 reg b0 = %08x, 00:00.0 reg b0 = %08x\n", pci_rd(0, 2, 0, 0xb0), pci_rd(0, 0, 0, 0xb0));
	gtt_tlb_flush();
	IOLog("gpustress: after TLB flush: pci 00:00.0 reg b0 = %08x\n", pci_rd(0, 0, 0, 0xb0));
	IOLog("gpustress: GTT pages %u..%u -> phys %08lx, aperture reads back %08x (dst) %08x (tex)\n", gBase, gBase + 8, gPhys, gAp[0], gAp[PG_TEX * 1024]);
#ifdef GS_APPLE
	{
		unsigned long dphys = 0, tphys = 0;
		void *dbuf = IOMallocContiguous(768 * 4096ul, 4096, &dphys);
		void *tbuf = IOMallocContiguous(64 * 4096ul, 4096, &tphys);
		unsigned int tbase = 14336;

		if (!dbuf || !tbuf) {
			IOLog("gpustress: no memory for apple surfaces\n");
			return KERN_SUCCESS;
		}
		for (j = 0; j < 768 * 1024; j++)
			((unsigned int *)dbuf)[j] = 0xff404040u;
		for (j = 0; j < 256 * 256; j++)
			((unsigned int *)tbuf)[j] = 0xff000000u | (j * 2654435761u >> 8);
		for (j = 0; j < 768; j++)
			gMM[0x10000 / 4 + 16384 + j] = ((unsigned int)dphys + j * 4096u) | 1;
		for (j = 0; j < 64; j++)
			gMM[0x10000 / 4 + tbase + j] = ((unsigned int)tphys + j * 4096u) | 1;
		gTexG = tbase * 4096u;
		__asm__ __volatile__("wbinvd" : : : "memory");
		gMM[0x2020 / 4] = pctl;
		(void)gMM[0x2020 / 4];
		gtt_tlb_flush();
		IOLog("gpustress: apple surfaces: colour 3MB at GTT 04000000 (phys %08lx), texture 256KB at %08x (phys %08lx)\n", dphys, gTexG, tphys);
	}
#endif
	gRunning = 1;
	IOCreateThread(worker, 0);
	return KERN_SUCCESS;
}

static kern_return_t
gpustress_stop(kmod_info_t *ki, void *data)
{
	return gRunning ? KERN_FAILURE : KERN_SUCCESS;
}

KMOD_EXPLICIT_DECL(com.local.gpustress, "0.1.0", gpustress_start, gpustress_stop)
