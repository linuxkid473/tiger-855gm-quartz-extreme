/* gpupoke: try to un-wedge a stalled 855GM ring by poking registers; logs HEAD/TAIL/INSTDONE after each step ("gpupoke:" in dmesg) */
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
static void pci_wr(unsigned b, unsigned d, unsigned f, unsigned r, unsigned v) { outl_(0xcf8, 0x80000000u | (b << 16) | (d << 11) | (f << 8) | (r & 0xfc)); outl_(0xcfc, v); }
static volatile unsigned int *mm;
static void st(const char *tag) { IOLog("gpupoke: %-28s HEAD %08x TAIL %08x INSTDONE %08x ESR %08x EIR %08x PGTBL_ER %08x\n", tag, mm[0x2034/4], mm[0x2030/4], mm[0x2090/4], mm[0x20b8/4], mm[0x20b0/4], mm[0x2024/4]); }
static kern_return_t
gpupoke_start(kmod_info_t *ki, void *data)
{
	unsigned int bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu, v, tail;
	mm = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
	if (!mm) return KERN_SUCCESS;
	st("start");
	mm[0x2024/4] = mm[0x2024/4]; mm[0x20b8/4] = mm[0x20b8/4]; mm[0x20b0/4] = mm[0x20b0/4]; IOSleep(300); st("cleared PGTBL_ER/ESR/EIR");
	v = pci_rd(0, 0, 0, 0xb0); pci_wr(0, 0, 0, 0xb0, v & ~0x80u); (void)pci_rd(0, 0, 0, 0xb0); pci_wr(0, 0, 0, 0xb0, v | 0x80u); IOSleep(300); st("GTLB toggle (host 0xb0 b7)");
	mm[0x2170/4] = 1; IOSleep(300); st("GFX_FLSH_CNTL=1 (0x2170)");
	mm[0x20e4/4] = 0x00020002; mm[0x20e4/4] = 0x00020000; IOSleep(300); st("MI_ARB toggle");
	tail = mm[0x2030/4]; mm[0x2030/4] = tail; IOSleep(300); st("rewrite TAIL");
	mm[0x20c0/4] = 0x00ff0000; IOSleep(300); st("MI_MODE mask-clear (0x20c0)");
	mm[0x2090/4] = 0; IOSleep(300); st("write INSTDONE");
	mm[0x6204/4] = 0; IOSleep(100); mm[0x6204/4] = 1; IOSleep(300); st("RENCLK_GATE_D1 0 then 1");
	v = mm[0x2040/4]; mm[0x2040/4] = v; IOSleep(300); st("rewrite ACTHD-ish");
	return KERN_SUCCESS;
}
static kern_return_t gpupoke_stop(kmod_info_t *ki, void *data) { return KERN_SUCCESS; }
KMOD_EXPLICIT_DECL(com.local.gpupoke, "0.1.0", gpupoke_start, gpupoke_stop)
