/*
 * cpupin - keep a Pentium M (Enhanced SpeedStep) at its top performance state on Tiger/x86.
 *
 * Plain-C kernel extension (no IOKit classes). Every 100 ms:
 *   - read IA32_PERF_STATUS (0x198): [15:8] bus ratio, [7:0] VID of the current state
 *   - read IA32_THERM_STATUS (0x19C): bit 0 = the CPU's own thermal throttle (TM1/TM2) is active
 *   - if the state is not the top state and no thermal throttle is active, write the top state to
 *     IA32_PERF_CTL (0x199).
 * The CPU's hardware thermal protection is never overridden (nothing is written while bit 0 is set).
 * Every state change and every correction is logged (IOLog -> system.log, prefix "cpupin:").
 *
 * The top state value comes from this machine's own ACPI _PSS table as shown in ioreg
 * (Dell Inspiron 510m, Pentium M 1.6 GHz): 1600 MHz = control 0x1028 (ratio 16 x 100 MHz, VID 0x28).
 */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <libkern/libkern.h>

extern void IOLog(const char *fmt, ...);
extern int hz;
extern void timeout(void (*func)(void *), void *arg, int ticks);
extern void untimeout(void (*func)(void *), void *arg);

#define MSR_PERF_STATUS 0x198
#define MSR_PERF_CTL    0x199
#define MSR_THERM_STATUS 0x19c

#define TOP_STATE 0x1028u       /* 1600 MHz: ratio 0x10, VID 0x28 */
#define PIN_ENABLED 1

static inline unsigned int rd_lo(unsigned int msr)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return lo;
}
static inline void wr_lo(unsigned int msr, unsigned int lo)
{
    __asm__ __volatile__("wrmsr" : : "a"(lo), "d"(0), "c"(msr));
}

static int running;
static unsigned int last_state = 0xffffffffu, last_hot = 0xffffffffu;
static unsigned int checks, fixes, hot_checks, changes;
static int logged_this_min;
static unsigned int minute_checks;

static void tick(void *arg)
{
    unsigned int st, ratio, hot;
    if (!running) return;
    st = rd_lo(MSR_PERF_STATUS) & 0xffff;
    hot = rd_lo(MSR_THERM_STATUS) & 1;
    ratio = st >> 8;
    checks++; minute_checks++;
    if (hot) hot_checks++;
    if (st != last_state || hot != last_hot) {
        changes++;
        if (logged_this_min < 30) {
            IOLog("cpupin: state 0x%04x (ratio %u = %u MHz) thermal-throttle %u\n", st, ratio, ratio * 100, hot);
            logged_this_min++;
        }
        last_state = st; last_hot = hot;
    }
#if PIN_ENABLED
    if ((st & 0xffff) != TOP_STATE && !hot) {
        wr_lo(MSR_PERF_CTL, TOP_STATE);
        fixes++;
    }
#endif
    if (minute_checks >= 600) {          /* ~60 s */
        IOLog("cpupin: 60s: checks %u changes %u fixes %u thermal-active %u (cur 0x%04x)\n",
              checks, changes, fixes, hot_checks, st);
        minute_checks = 0; logged_this_min = 0;
    }
    timeout(tick, 0, hz / 10 ? hz / 10 : 1);
}

static kern_return_t cpupin_start(kmod_info_t *ki, void *d)
{
    unsigned int st = rd_lo(MSR_PERF_STATUS) & 0xffff;
    unsigned int hot = rd_lo(MSR_THERM_STATUS) & 1;
    IOLog("cpupin: loaded; start state 0x%04x (%u MHz) thermal-throttle %u, pinning to 0x%04x\n",
          st, (st >> 8) * 100, hot, TOP_STATE);
    running = 1;
    timeout(tick, 0, hz / 10 ? hz / 10 : 1);
    return KERN_SUCCESS;
}

static kern_return_t cpupin_stop(kmod_info_t *ki, void *d)
{
    running = 0;
    untimeout(tick, 0);
    IOLog("cpupin: unloaded\n");
    return KERN_SUCCESS;
}

KMOD_EXPLICIT_DECL(com.local.cpupin, "1.0.0", cpupin_start, cpupin_stop)
