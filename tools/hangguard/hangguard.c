/*
 * hangguard - reboot an unattended Inspiron 510m (ICH4-M + Intel 855GM) when it hangs.
 *
 * Plain-C kernel extension. NOTE: layer 1 below is implemented (tco_arm) but NOT used: on this Dell the TCO
 * second timeout never resets the machine (tested with NO_REBOOT cleared, TCO SMI and interrupt disabled).
 * The shipped behaviour is layer 2 + a direct 0xCF9 hard reset. A full kernel deadlock cannot be caught.
 *
 *  1. Hardware watchdog (unused): the ICH4 TCO timer (I/O ports PMBASE+0x60..). It is armed with a ~9 s tick; its
 *     second expiry resets the machine (~18 s). A kernel timer reloads it once per second, so any state in
 *     which kernel timers stop firing (kernel deadlock, spin with interrupts off, panic halt) ends in a reboot.
 *
 *  2. GPU hang detector: every second the 855GM ring registers are sampled. If the ring HEAD and ACTHD do not
 *     move for GPU_STUCK_SECS while HEAD != TAIL (work queued, nothing executing), the reload is withheld so
 *     the watchdog fires - this is the "GPU hard hang" (machine freezes, only a power cycle helped) case.
 *
 * The reset needs BOTH a stuck ring AND a latched GPU error register (EIR/ESR): a ring that merely stops while the display sleeps must not reboot the machine.
 * Safety:
 *  - GPU rule is only evaluated GRACE_SECS after load, and not at all if the previous reset was caused by the
 *    watchdog (TCO2_STS.BOOT_STS) - a persistent hang can therefore reboot every other boot at most.
 *  - If the chipset refuses to clear NO_REBOOT, a hard reset through port 0xCF9 is used instead (after
 *    RESET_DELAY_SECS of confirmed hang).
 *  - kextunload disables the watchdog again.
 * Log lines start with "hangguard:" (system.log / dmesg).
 */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <libkern/libkern.h>

extern void IOLog(const char *fmt, ...);
extern int hz;
extern void timeout(void (*func)(void *), void *arg, int ticks);
extern void untimeout(void (*func)(void *), void *arg);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);

#ifndef ARM
#define ARM 1                   /* 0 = probe only (log registers, never arm) */
#endif
#define GRACE_SECS      150
#define GPU_STUCK_SECS  20
#define TCO_TICKS       15      /* x0.6 s per TCO timeout */
#define RESET_DELAY_SECS 6

static inline void outb_(unsigned short p, unsigned char v) { __asm__ __volatile__("outb %0,%w1" : : "a"(v), "Nd"(p)); }
static inline void outw_(unsigned short p, unsigned short v) { __asm__ __volatile__("outw %0,%w1" : : "a"(v), "Nd"(p)); }
static inline void outl_(unsigned short p, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(p)); }
static inline unsigned char inb_(unsigned short p) { unsigned char v; __asm__ __volatile__("inb %w1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned short inw_(unsigned short p) { unsigned short v; __asm__ __volatile__("inw %w1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline unsigned int inl_(unsigned short p) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(p)); return v; }

static unsigned int pci_rd(unsigned bus, unsigned dev, unsigned fn, unsigned reg)
{ outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc)); return inl_(0xcfc); }
static void pci_wr(unsigned bus, unsigned dev, unsigned fn, unsigned reg, unsigned int v)
{ outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc)); outl_(0xcfc, v); }

static int running;
static unsigned pm_base, tco;       /* tco = 0 if the watchdog is unusable */
static int tco_armed, prev_wd, use_cf9;
static volatile unsigned int *mmio;
static unsigned int secs, stuck, last_head = 0xffffffffu, last_acthd = 0xffffffffu, resetting;
static unsigned int gpu_rule_on;

static void tco_reload(void) { if (tco) outb_(tco + 0x00, 0x01); }

static void tco_disable(void)
{
    if (tco) outw_(tco + 0x08, inw_(tco + 0x08) | 0x0800);      /* TCO_TMR_HLT */
    tco_armed = 0;
}

static void tco_arm(void)
{
    unsigned int v;
    unsigned char t;
    pci_wr(0, 31, 0, 0xd4, pci_rd(0, 31, 0, 0xd4) & ~2u);       /* NO_REBOOT = 0 */
    if (pci_rd(0, 31, 0, 0xd4) & 2u) {
        IOLog("hangguard: chipset keeps NO_REBOOT set; using port 0xCF9 reset instead of TCO\n");
        use_cf9 = 1;
        return;
    }
    outl_(pm_base + 0x30, inl_(pm_base + 0x30) & ~0x00002000u);   /* TCO_EN off: no BIOS SMI on the first timeout */
    IOLog("hangguard: SMI_EN now %08x\n", inl_(pm_base + 0x30));
    outw_(tco + 0x0a, (inw_(tco + 0x0a) & ~0x0006u) | 0x0006u);   /* TCO_INT_SEL = 11: no interrupt on first timeout */
    IOLog("hangguard: TCO2_CNT now %04x\n", inw_(tco + 0x0a));
    t = inb_(tco + 0x01);
    outb_(tco + 0x01, (t & 0xc0) | TCO_TICKS);
    outw_(tco + 0x04, 0x0008);                                  /* clear TIMEOUT */
    outw_(tco + 0x06, 0x0002);                                  /* clear SECOND_TO_STS */
    outw_(tco + 0x06, 0x0004);                                  /* clear BOOT_STS */
    tco_reload();
    v = inw_(tco + 0x08);
    outw_(tco + 0x08, v & ~0x0800u);                            /* run */
    tco_armed = 1;
    IOLog("hangguard: TCO armed, %u x 0.6 s per timeout, CNT1 %04x TMR %02x\n", TCO_TICKS, inw_(tco + 0x08), inb_(tco + 0x01));
}

static void hard_reset(void)
{
    IOLog("hangguard: hard reset via 0xCF9\n");
    outb_(0xcf9, 0x06);
    outb_(0xcf9, 0x0e);
}

static void tick(void *arg)
{
    unsigned int head, tail, acthd;
    int hung = 0;
    if (!running) return;
    secs++;
    if (mmio) {
        head = mmio[0x2034 / 4] & 0x001ffffcu;
        tail = mmio[0x2030 / 4] & 0x001ffffcu;
        acthd = mmio[0x2074 / 4];
        if (head != tail && head == last_head && acthd == last_acthd) stuck++; else stuck = 0;
        last_head = head; last_acthd = acthd;
        if (stuck && (stuck % 5) == 0)
            IOLog("hangguard: GPU ring not moving for %u s (HEAD %06x TAIL %06x ACTHD %08x EIR %08x ESR %08x)\n",
                  stuck, head, tail, acthd, mmio[0x20b0 / 4], mmio[0x20b8 / 4]);
        /* a stuck ring alone is not enough (the GPU can legitimately sit on queued work, e.g. while the display sleeps):
         * the real hangs also latched an error (EIR/ESR) */
        if (gpu_rule_on && stuck >= GPU_STUCK_SECS && (mmio[0x20b0 / 4] || mmio[0x20b8 / 4])) hung = 1;
    }
    if (!gpu_rule_on && secs >= GRACE_SECS && !prev_wd) {
        gpu_rule_on = 1;
        IOLog("hangguard: GPU hang rule active\n");
    }
#if ARM
    if (hung) {
        if (!resetting) IOLog("hangguard: GPU hang confirmed (%u s) - rebooting\n", stuck);
        resetting++;
        if (use_cf9 && resetting >= RESET_DELAY_SECS) hard_reset();
        /* TCO mode: simply stop reloading; the watchdog resets the machine */
    } else if (tco_armed) {
#ifndef NOKICK
        tco_reload();
#endif
    }
#endif
#ifdef TRACE
    if (secs < 45 && tco) IOLog("hangguard: t=%u RLD %02x STS1 %04x STS2 %04x CNT1 %04x D4 %08x\n", secs, inb_(tco), inw_(tco + 4), inw_(tco + 6), inw_(tco + 8), pci_rd(0, 31, 0, 0xd4));
#endif
    if ((secs % 300) == 0)
        IOLog("hangguard: alive %u s, stuck %u, TCO1_STS %04x TCO2_STS %04x\n", secs, stuck, tco ? inw_(tco + 4) : 0, tco ? inw_(tco + 6) : 0);
    timeout(tick, 0, hz ? hz : 100);
}

static kern_return_t hangguard_start(kmod_info_t *ki, void *d)
{
    unsigned int id = pci_rd(0, 31, 0, 0x00), acpi;
    unsigned int bar1 = pci_rd(0, 2, 0, 0x14) & ~0xfu;

    pm_base = pci_rd(0, 31, 0, 0x40) & 0xff80u;
    acpi = pci_rd(0, 31, 0, 0x44) & 0xff;
    IOLog("hangguard: LPC %08x PMBASE %04x ACPI_CNTL %02x GEN_STA/D4 %08x\n", id, pm_base, acpi, pci_rd(0, 31, 0, 0xd4));
    if ((id & 0xffff) == 0x8086 && pm_base && (acpi & 0x10)) {
        tco = pm_base + 0x60;
        IOLog("hangguard: TCO RLD/TMR %04x TCO1_STS %04x TCO2_STS %04x TCO1_CNT %04x TCO2_CNT %04x SMI_EN %08x SMI_STS %08x\n",
              inw_(tco), inw_(tco + 4), inw_(tco + 6), inw_(tco + 8), inw_(tco + 10), inl_(pm_base + 0x30), inl_(pm_base + 0x34));
        prev_wd = (inw_(tco + 6) & 0x0004) ? 1 : 0;
        if (prev_wd) IOLog("hangguard: previous reset was caused by the watchdog (BOOT_STS) - GPU rule stays off this boot\n");
    } else
        IOLog("hangguard: no usable ICH TCO\n");
    if (bar1) mmio = (volatile unsigned int *)ml_io_map(bar1, 0x80000);
    if (mmio)
        IOLog("hangguard: GPU HEAD %08x TAIL %08x ACTHD %08x EIR %08x\n", mmio[0x2034 / 4], mmio[0x2030 / 4], mmio[0x2074 / 4], mmio[0x20b0 / 4]);
#if ARM
    use_cf9 = 1;   /* the ICH4 TCO second timeout does not reset this machine (tested), so reset via 0xCF9 */
#endif
    running = 1;
    timeout(tick, 0, hz ? hz : 100);
    IOLog("hangguard: loaded (ARM=%d, grace %d s, GPU stuck limit %d s)\n", ARM, GRACE_SECS, GPU_STUCK_SECS);
    return KERN_SUCCESS;
}

static kern_return_t hangguard_stop(kmod_info_t *ki, void *d)
{
    running = 0;
    untimeout(tick, 0);
    tco_disable();
    IOLog("hangguard: unloaded, watchdog stopped\n");
    return KERN_SUCCESS;
}

KMOD_EXPLICIT_DECL(com.local.hangguard, "1.0.0", hangguard_start, hangguard_stop)
