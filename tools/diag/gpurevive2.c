/* gpurevive2: try to recover a hung 855GM ring without a reboot ("gpurevive2:" in dmesg).
 * steps: disable ring, clear errors, HEAD = TAIL (skip the queued work), re-enable, write the latest stamp into the status page */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>
extern void IOLog(const char *fmt, ...);
extern void IOSleep(unsigned int ms);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);
void __bzero(void *p, unsigned long n) { volatile unsigned char *q = (volatile unsigned char *)p; while (n--) *q++ = 0; }
static inline void outl_(unsigned short port, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(port)); }
static inline unsigned int inl_(unsigned short port) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v; }
static unsigned int pci_rd(unsigned b, unsigned d, unsigned f, unsigned r) { outl_(0xcf8, 0x80000000u | (b << 16) | (d << 11) | (f << 8) | (r & 0xfc)); return inl_(0xcfc); }
static volatile unsigned int *mm;
static void st(const char *tag) { IOLog("gpurevive2: %-26s HEAD %08x TAIL %08x CTL %08x INSTDONE %08x ESR %08x EIR %08x\n", tag, mm[0x2034/4], mm[0x2030/4], mm[0x203c/4], mm[0x2090/4], mm[0x20b8/4], mm[0x20b0/4]); }
static kern_return_t
gpurevive2_start(kmod_info_t *ki, void *data)
{
	unsigned int bar0 = pci_rd(0, 2, 0, 0x10) & ~0xfu, bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu;
	unsigned int ctl, tail, start, hws, stamp = 0, off, i;
	volatile unsigned int *ring, *sp = 0;
	mm = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
	if (!mm || !bar0) return KERN_SUCCESS;
	st("before");
	ctl = mm[0x203c/4]; tail = mm[0x2030/4] & 0x1ffffc; start = mm[0x2038/4] & ~0xfffu; hws = mm[0x2080/4];
	IOLog("gpurevive2: CTL %08x TAIL %08x START %08x HWS_PGA %08x\n", ctl, tail, start, hws);
	{	/* ring and status page through the aperture */
		unsigned int rb = start & ~0x1ffffffu, hb = hws & ~0x1ffffffu;
		volatile unsigned int *a = (volatile unsigned int *)ml_io_map(bar0 + rb, 0x2000000);
		volatile unsigned int *h = (volatile unsigned int *)ml_io_map(bar0 + hb, 0x2000000);
		if (a) ring = a + ((start - rb) >> 2);
		if (h) sp = h + (((hws & ~0xfffu) - hb) >> 2);
		if (sp) IOLog("gpurevive2: status page dwords 0..20: %08x %08x %08x %08x | %08x %08x %08x %08x | stamp@0x40 %08x\n", sp[0], sp[1], sp[2], sp[3], sp[4], sp[5], sp[6], sp[7], sp[0x40/4]);
		if (a) {	/* newest stamp in the ring: scan backwards from TAIL for 10800001 00000040 <stamp> */
			for (i = 0, off = tail; i < 1024 && !stamp; i++) {
				unsigned int o = (off - 12 * (i + 1) / 3 * 0) ;
				(void)o;
			}
			for (i = 1; i < 1000 && !stamp; i++) {
				unsigned int p = ((tail >> 2) + 1024 - i) & 1023;
				if (ring[p] == 0x10800001 && ring[(p + 1) & 1023] == 0x40) stamp = ring[(p + 2) & 1023];
			}
			IOLog("gpurevive2: newest stamp in ring %08x\n", stamp);
		}
	}
	mm[0x203c/4] = 0; IOSleep(100); st("ring disabled");
	{ unsigned int e; mm[0x20b4/4] = 0; IOSleep(50); e = mm[0x20b0/4]; IOLog("gpurevive2: EIR after unmask %08x\n", e); mm[0x20b0/4] = e; mm[0x2024/4] = mm[0x2024/4]; mm[0x20b8/4] = mm[0x20b8/4]; IOSleep(50); IOLog("gpurevive2: EIR now %08x ESR %08x\n", mm[0x20b0/4], mm[0x20b8/4]); mm[0x20b4/4] = 0xffffffff; } st("errors cleared");
	mm[0x2034/4] = tail; mm[0x2030/4] = tail; IOSleep(50); st("HEAD=TAIL");
	mm[0x2038/4] = start | (mm[0x2038/4] & 0xfff);
	mm[0x203c/4] = ctl | 1; IOSleep(100); st("ring enabled");
	
	IOSleep(500); st("after 0.5 s");
	return KERN_SUCCESS;
}
static kern_return_t gpurevive2_stop(kmod_info_t *ki, void *data) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(com.local.gpurevive2, "0.1.0", gpurevive2_start, gpurevive2_stop)
