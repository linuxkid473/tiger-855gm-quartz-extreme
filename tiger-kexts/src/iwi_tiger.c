/*
 * iwi for Mac OS X Tiger 10.4.x (i386): Intel PRO/Wireless 2200BG (8086:4220) as a BSD network interface.
 *
 * A single-file, plain-C kernel extension (no C++/IOKit classes).  It is a port of the ZETA iwi driver + iwid
 * supplicant: the card's firmware does scan/authenticate/associate, WPA2-PSK (CCMP) is done here in software,
 * and the result is presented as an Ethernet interface (en1) through the ifnet KPI.
 *
 * Register knowledge: OpenBSD/FreeBSD if_iwi (ISC licence, Damien Bergamini); Linux ipw2200.
 * Control through sysctl:   sysctl -w net.iwi.ssid=NAME  net.iwi.psk=PASSPHRASE  net.iwi.cmd=9     (see iwi-wifi)
 */
#include <mach/mach_types.h>
#include <mach/kmod.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kpi_mbuf.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/kpi_interface.h>
#include <net/ethernet.h>
#include <kern/locks.h>

#define sha1_init iwi_sha1_init
#define sha1_update iwi_sha1_update
#define sha1_final iwi_sha1_final
#define sha1 iwi_sha1
#define hmac_sha1 iwi_hmac_sha1
#define pbkdf2_sha1 iwi_pbkdf2_sha1
#define wpa_prf iwi_wpa_prf
#define aes_set_key iwi_aes_set_key
#define aes_encrypt iwi_aes_encrypt
#define aes_decrypt iwi_aes_decrypt
#define aes_unwrap iwi_aes_unwrap
#define ccm_encrypt iwi_ccm_encrypt
#define ccm_decrypt iwi_ccm_decrypt
#include "wpa.h"

typedef unsigned short u16;

extern void bcopy(const void *, void *, unsigned long);
extern void bzero(void *, unsigned long);
extern int bcmp(const void *, const void *, unsigned long);
extern void IOLog(const char *fmt, ...);
extern void IOSleep(unsigned int ms);
extern void IODelay(unsigned int us);
extern void *IOMallocContiguous(unsigned long size, unsigned long alignment, unsigned long *phys);
extern void *IOCreateThread(void (*fn)(void *), void *arg);
extern unsigned long ml_io_map(unsigned long phys, unsigned long size);
extern void read_random(void *buf, unsigned int numbytes);
extern void microuptime(struct timeval *tv);

/* ether family helpers exported by the kernel (not in the public KPI headers) */
extern errno_t ether_demux(ifnet_t, mbuf_t, char *, protocol_family_t *);
extern errno_t ether_add_proto(ifnet_t, protocol_family_t, const struct ifnet_demux_desc *, u_int32_t);
extern errno_t ether_del_proto(ifnet_t, protocol_family_t);
extern errno_t ether_check_multi(ifnet_t, const struct sockaddr *);
extern errno_t ether_frameout(ifnet_t, mbuf_t *, const struct sockaddr *, const char *, const char *);

#include "fw_bss.h"		/* generated: static const unsigned char iwi_fw[]; iwi_fw_len */

/* the compiler lowers memset(x,0,n) to this; Tiger's kernel only exports bzero */
void
__bzero(void *p, unsigned long n)
{
	volatile unsigned char *q = (volatile unsigned char *)p;

	while (n--)
		*q++ = 0;
}

/* ------------------------------------------------------------------ log ring (also readable via sysctl) */

#define LOGSZ 6144
static char gLog[LOGSZ];
static int gLogLen = 0;
static int gVerbose = 1;
static volatile int gIOLog = 0;	/* IOLog from a burst of calls in the network paths is suspect: log to the sysctl ring only */

static void
say(const char *fmt, ...)
{
	char line[200];
	__builtin_va_list ap;
	int n, k;

	__builtin_va_start(ap, fmt);
	n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
	__builtin_va_end(ap);
	if (n < 0) n = 0;
	if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
	line[n++] = '\n';
	line[n] = 0;
	if (gIOLog)
		IOLog("iwi: %s", line);
	if (gLogLen + n >= LOGSZ) {		/* drop the oldest half */
		k = LOGSZ / 2;
		bcopy(gLog + k, gLog, gLogLen - k);
		gLogLen -= k;
	}
	bcopy(line, gLog + gLogLen, n);
	gLogLen += n;
	gLog[gLogLen] = 0;
}

static char gCrumb[64] = "-";
static volatile unsigned int gBeat = 0, gCrumbN = 0;
static void
crumb(const char *fmt, ...)
{
	__builtin_va_list ap;

	__builtin_va_start(ap, fmt);
	vsnprintf(gCrumb, sizeof(gCrumb), fmt, ap);
	__builtin_va_end(ap);
	gCrumbN++;
}

static unsigned long long
now_us(void)
{
	struct timeval tv;

	microuptime(&tv);
	return (unsigned long long)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void
snooze(unsigned int us)
{
	if (us >= 2000)
		IOSleep((us + 999) / 1000);
	else
		IODelay(us);
}

static void
random_bytes(u8 *out, int n)
{
	read_random(out, n);
}

/* ------------------------------------------------------------------ PCI config space via I/O ports */

static inline void outl_(unsigned short port, unsigned int v) { __asm__ __volatile__("outl %0,%w1" : : "a"(v), "Nd"(port)); }
static inline unsigned int inl_(unsigned short port) { unsigned int v; __asm__ __volatile__("inl %w1,%0" : "=a"(v) : "Nd"(port)); return v; }

static u32
pci_rd(int bus, int dev, int fn, int reg)
{
	outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc));
	return inl_(0xcfc);
}

static void
pci_wr(int bus, int dev, int fn, int reg, u32 v)
{
	outl_(0xcf8, 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xfc));
	outl_(0xcfc, v);
}

/* ------------------------------------------------------------------ hardware layer (from iwi.c) */

#define CSR_INTR		0x0008
#define CSR_INTR_MASK		0x000c
#define CSR_INDIRECT_ADDR	0x0010
#define CSR_INDIRECT_DATA	0x0014
#define CSR_AUTOINC_ADDR	0x0018
#define CSR_AUTOINC_DATA	0x001c
#define CSR_RST			0x0020
#define CSR_CTL			0x0024
#define CSR_IO			0x0030
#define CSR_READ_INT		0x0ff4

#define RST_PRINCETON_RESET	0x00000001
#define RST_SW_RESET		0x00000080
#define RST_MASTER_DISABLED	0x00000100
#define RST_STOP_MASTER		0x00000200
#define CTL_CLOCK_READY		0x00000001
#define CTL_INIT		0x00000004
#define READ_INT_INIT_HOST	0x20000000

#define MEM_EEPROM_CTL		0x00300040
#define EEPROM_C		(1 << 0)
#define EEPROM_S		(1 << 1)
#define EEPROM_D		(1 << 2)
#define EEPROM_Q		(1 << 4)
#define EEPROM_MAC		0x21

#define MEM_EVENT_CTL		0x00300004
#define INTR_FW_INITED		0x01000000
#define INTR_FATAL_ERROR	0x40000000
#define INTR_PARITY_ERROR	0x80000000
#define CB_DEFAULT_CTL		0x8cea0000
#define CB_MAXDATALEN		8191
#define FW_MAX			0x30000
#define RING_AREA_SIZE		0x24000

#define CMD_DESC_SIZE		128
#define CMD_RING_COUNT		16
#define RX_COUNT		32
#define RX_BUF_SIZE		2048
#define RX_BUF_OFFSET		0x11000
#define TX_RING_OFFSET		0x1000
#define TX_DESC_SIZE		128
#define TX_RING_COUNT		64
#define TX_BUF_SIZE		2048
#define TX_AREA_SIZE		(TX_RING_COUNT * TX_BUF_SIZE)

#define CSR_CMD_RIDX		0x0280
#define CSR_CMD_WIDX		0x0f80
#define CSR_TX_RIDX0		0x0284
#define CSR_TX_WIDX0		0x0f84
#define CSR_RX_RIDX		0x02a0
#define CSR_RX_WIDX		0x0fa0

#define HDR_TYPE_COMMAND	1
#define HDR_TYPE_NOTIF		3
#define HDR_TYPE_FRAME		9
#define HDR_FLAG_IRQ		0x04

static int gBus = -1, gDev, gFn;
static volatile u8 *gRegs = 0;
static u8 *gDma, *gRing, *gTxBuf;
static unsigned long gDmaPhys, gRingPhys, gTxPhys;
static u32 gCmdCur, gRxCur, gTxCur, gIntrSeen;
static int gUp = 0;
static u8 gMac[6];
static int gMacKnown = 0;
static u32 gPciId = 0;

static u32 csr_read(u32 reg) { return *(volatile u32 *)(gRegs + reg); }
static void csr_write(u32 reg, u32 val) { *(volatile u32 *)(gRegs + reg) = val; }
static u32 mem_read(u32 addr) { csr_write(CSR_INDIRECT_ADDR, addr); return csr_read(CSR_INDIRECT_DATA); }
static void mem_write(u32 addr, u32 val) { csr_write(CSR_INDIRECT_ADDR, addr); csr_write(CSR_INDIRECT_DATA, val); }
static u8 mem_read1(u32 addr) { csr_write(CSR_INDIRECT_ADDR, addr); return *(volatile u8 *)(gRegs + CSR_INDIRECT_DATA); }
static void mem_write1(u32 addr, u8 val) { csr_write(CSR_INDIRECT_ADDR, addr); *(volatile u8 *)(gRegs + CSR_INDIRECT_DATA) = val; }
static void mem_write2(u32 addr, u16 val) { csr_write(CSR_INDIRECT_ADDR, addr); *(volatile u16 *)(gRegs + CSR_INDIRECT_DATA) = val; }

static void eeprom_ctl(u32 val) { mem_write(MEM_EEPROM_CTL, val); IODelay(1); }

static u16
read_prom_word(u8 addr)
{
	u32 tmp;
	u16 val = 0;
	int n;

	eeprom_ctl(0);
	eeprom_ctl(EEPROM_S);
	eeprom_ctl(EEPROM_S | EEPROM_C);
	eeprom_ctl(EEPROM_S);
	eeprom_ctl(EEPROM_S | EEPROM_D);
	eeprom_ctl(EEPROM_S | EEPROM_D | EEPROM_C);
	eeprom_ctl(EEPROM_S | EEPROM_D);
	eeprom_ctl(EEPROM_S | EEPROM_D | EEPROM_C);
	eeprom_ctl(EEPROM_S);
	eeprom_ctl(EEPROM_S | EEPROM_C);
	for (n = 7; n >= 0; n--) {
		eeprom_ctl(EEPROM_S | (((addr >> n) & 1) << 2));
		eeprom_ctl(EEPROM_S | (((addr >> n) & 1) << 2) | EEPROM_C);
	}
	eeprom_ctl(EEPROM_S);
	for (n = 15; n >= 0; n--) {
		eeprom_ctl(EEPROM_S | EEPROM_C);
		eeprom_ctl(EEPROM_S);
		tmp = mem_read(MEM_EEPROM_CTL);
		val |= ((tmp & EEPROM_Q) >> 4) << n;
	}
	eeprom_ctl(0);
	eeprom_ctl(EEPROM_S);
	eeprom_ctl(0);
	eeprom_ctl(EEPROM_C);
	return val;
}

static u32 le32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

static int
stop_master_only(void)
{
	int ntries;
	u32 tmp;

	tmp = csr_read(CSR_RST);
	csr_write(CSR_RST, tmp | RST_STOP_MASTER);
	for (ntries = 0; ntries < 5; ntries++) {
		if (csr_read(CSR_RST) & RST_MASTER_DISABLED)
			return 0;
		IODelay(10);
	}
	return -1;
}

static int
stop_master(void)
{
	int r;
	u32 tmp;

	csr_write(CSR_INTR_MASK, 0);
	r = stop_master_only();
	tmp = csr_read(CSR_RST);
	csr_write(CSR_RST, tmp | RST_PRINCETON_RESET);
	return r;
}

static int
load_ucode(const u8 *data, int size)
{
	int ntries, i;
	u32 tmp;
	const u8 *p;

	if (stop_master_only() < 0)
		return -1;
	mem_write(0x3000e0, 0x80000000);
	snooze(5000);
	tmp = csr_read(CSR_RST);
	csr_write(CSR_RST, tmp & ~RST_PRINCETON_RESET);
	snooze(5000);
	mem_write(0x3000e0, 0);
	snooze(1000);
	mem_write(MEM_EVENT_CTL, 1);
	snooze(1000);
	mem_write(MEM_EVENT_CTL, 0);
	snooze(1000);
	mem_write1(0x200000, 0x00);
	mem_write1(0x200000, 0x40);
	snooze(1000);
	for (p = data; size > 0; p += 2, size -= 2)
		mem_write2(0x200010, p[0] | (p[1] << 8));
	mem_write1(0x200000, 0x00);
	mem_write1(0x200000, 0x80);
	for (ntries = 0; ntries < 100; ntries++) {
		if (mem_read1(0x200000) & 1)
			break;
		IODelay(100);
	}
	if (ntries == 100)
		return -2;
	for (i = 0; i < 7; i++)
		(void)mem_read(0x200004);
	mem_write1(0x200000, 0x00);
	return 0;
}

static int
load_firmware(const u8 *data, int size, u32 *intr_seen)
{
	const u8 *p, *end;
	u32 sentinel, tmp, ctl, src, dst, sum, len, mlen;
	int ntries;

	if (size > FW_MAX)
		return -10;
	bcopy(data, gDma, size);
	mem_write(0x3000a0, 0x27000);
	src = gDmaPhys;
	p = gDma;
	end = p + size;
	csr_write(CSR_AUTOINC_ADDR, 0x27000);
	while (p < end) {
		dst = le32(p); p += 4; src += 4;
		len = le32(p); p += 4; src += 4;
		p += len;
		while (len > 0) {
			mlen = len < CB_MAXDATALEN ? len : CB_MAXDATALEN;
			ctl = CB_DEFAULT_CTL | mlen;
			sum = ctl ^ src ^ dst;
			csr_write(CSR_AUTOINC_DATA, ctl);
			csr_write(CSR_AUTOINC_DATA, src);
			csr_write(CSR_AUTOINC_DATA, dst);
			csr_write(CSR_AUTOINC_DATA, sum);
			src += mlen;
			dst += mlen;
			len -= mlen;
		}
	}
	sentinel = csr_read(CSR_AUTOINC_ADDR);
	csr_write(CSR_AUTOINC_DATA, 0);
	tmp = csr_read(CSR_RST);
	tmp &= ~(RST_MASTER_DISABLED | RST_STOP_MASTER);
	csr_write(CSR_RST, tmp);
	mem_write(0x3000a4, 0x540100);
	for (ntries = 0; ntries < 400; ntries++) {
		if (mem_read(0x3000d0) >= sentinel)
			break;
		IODelay(100);
	}
	if (ntries == 400)
		return -11;
	mem_write(0x3000a4, 0x540c00);
	csr_write(CSR_RST, 0);
	tmp = csr_read(CSR_CTL);
	csr_write(CSR_CTL, tmp | 2);
	*intr_seen = 0;
	for (ntries = 0; ntries < 1000; ntries++) {
		tmp = csr_read(CSR_INTR);
		if (tmp != 0 && tmp != 0xffffffff) {
			*intr_seen |= tmp;
			csr_write(CSR_INTR, tmp);
		}
		if (*intr_seen & (INTR_FW_INITED | INTR_FATAL_ERROR | INTR_PARITY_ERROR))
			break;
		IODelay(1000);
	}
	if (*intr_seen & (INTR_FATAL_ERROR | INTR_PARITY_ERROR))
		return -12;
	if (!(*intr_seen & INTR_FW_INITED))
		return -13;
	return 0;
}

static void
ack_intr(void)
{
	u32 intr = csr_read(CSR_INTR);

	if (intr != 0 && intr != 0xffffffff) {
		gIntrSeen |= intr;
		csr_write(CSR_INTR, intr);
	}
}

static int
fw_cmd(unsigned char type, const void *data, int len)
{
	u8 *d = gRing + gCmdCur * CMD_DESC_SIZE;
	int ntries;
	u32 next = (gCmdCur + 1) % CMD_RING_COUNT;

	if (!gUp || len > 120)
		return -100;
	bzero(d, CMD_DESC_SIZE);
	d[0] = HDR_TYPE_COMMAND;
	d[2] = HDR_FLAG_IRQ;
	d[4] = type;
	d[5] = len;
	if (len > 0)
		bcopy(data, d + 8, len);
	gCmdCur = next;
	csr_write(CSR_CMD_WIDX, next);
	for (ntries = 0; ntries < 1000; ntries++) {
		ack_intr();
		if (csr_read(CSR_CMD_RIDX) == next)
			return 0;
		snooze(1000);
	}
	return -1;
}

/* find the card, enable memory + bus master, map the register window */
static int
hw_probe(void)
{
	int bus, dev;
	u32 id, cmd, bar;

	if (gRegs)
		return 0;
	for (bus = 0; bus < 8 && gBus < 0; bus++) {
		for (dev = 0; dev < 32; dev++) {
			id = pci_rd(bus, dev, 0, 0);
			if ((id & 0xffff) == 0x8086 && ((id >> 16) == 0x4220 || (id >> 16) == 0x4221 ||
			    (id >> 16) == 0x4223 || (id >> 16) == 0x4224)) {
				gBus = bus; gDev = dev; gFn = 0; gPciId = id;
				break;
			}
		}
	}
	if (gBus < 0) {
		say("probe: no 2200BG found");
		return -1;
	}
	cmd = pci_rd(gBus, gDev, gFn, 4);
	bar = pci_rd(gBus, gDev, gFn, 0x10) & ~0xfu;
	say("probe: %x at %d:%d.%d, command %08x, BAR0 %08x", id, gBus, gDev, gFn, cmd, bar);
	say("probe: irq line/pin %08x", pci_rd(gBus, gDev, gFn, 0x3c));
	if (bar == 0 || bar == 0xfffffff0u)
		return -2;
	pci_wr(gBus, gDev, gFn, 4, (cmd & 0xffff) | 0x6 | 0x400);		/* memory + bus master + INTx disable (we poll; the line must stay quiet) */
	pci_wr(gBus, gDev, gFn, 0x40, pci_rd(gBus, gDev, gFn, 0x40) & ~0x0000ff00u);
	gRegs = (volatile u8 *)ml_io_map(bar, 4096);
	if (gRegs == 0)
		return -3;
	say("probe: registers mapped at %p, CTL %08x RST %08x", gRegs, csr_read(CSR_CTL), csr_read(CSR_RST));
	return 0;
}

static int
hw_reset(void)
{
	u32 tmp;
	u16 w;
	int ntries, i;

	if (hw_probe() != 0)
		return -1;
	stop_master();
	tmp = csr_read(CSR_CTL);
	csr_write(CSR_CTL, tmp | CTL_INIT);
	csr_write(CSR_READ_INT, READ_INT_INIT_HOST);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (csr_read(CSR_CTL) & CTL_CLOCK_READY)
			break;
		IODelay(200);
	}
	if (ntries == 1000)
		return -6;
	tmp = csr_read(CSR_RST);
	csr_write(CSR_RST, tmp | RST_SW_RESET);
	IODelay(10);
	tmp = csr_read(CSR_CTL);
	csr_write(CSR_CTL, tmp | CTL_INIT);
	csr_write(CSR_AUTOINC_ADDR, 0);
	for (i = 0; i < 0xc000; i++)
		csr_write(CSR_AUTOINC_DATA, 0);
	w = read_prom_word(EEPROM_MAC + 0);
	gMac[0] = w & 0xff; gMac[1] = w >> 8;
	w = read_prom_word(EEPROM_MAC + 1);
	gMac[2] = w & 0xff; gMac[3] = w >> 8;
	w = read_prom_word(EEPROM_MAC + 2);
	gMac[4] = w & 0xff; gMac[5] = w >> 8;
	gMacKnown = 1;
	return 0;
}

static int
alloc_dma(void)
{
	if (gDma == 0) {
		gDma = IOMallocContiguous(FW_MAX, 4096, &gDmaPhys);
		if (gDma == 0) return -7;
	}
	if (gRing == 0) {
		gRing = IOMallocContiguous(RING_AREA_SIZE, 4096, &gRingPhys);
		if (gRing == 0) return -7;
	}
	if (gTxBuf == 0) {
		gTxBuf = IOMallocContiguous(TX_AREA_SIZE, 4096, &gTxPhys);
		if (gTxBuf == 0) return -7;
	}
	return 0;
}

static int
do_up(void)
{
	u32 seen, bootsz, ucodesz, mainsz;
	const u8 *fw;
	int r, i, ac;

	gUp = 0;
	gIntrSeen = 0;
	if (iwi_fw_len < 16 || iwi_fw[2] < 3)
		return -2;
	bootsz = le32(iwi_fw + 4);
	ucodesz = le32(iwi_fw + 8);
	mainsz = le32(iwi_fw + 12);
	if (iwi_fw_len < 16 + bootsz + ucodesz + mainsz || bootsz == 0 || ucodesz == 0 || mainsz == 0)
		return -3;
	if ((r = hw_reset()) != 0)
		return r;
	if ((r = alloc_dma()) != 0)
		return r;
	bzero(gRing, RING_AREA_SIZE);
	gCmdCur = gRxCur = gTxCur = 0;
	say("phys: dma %lx ring %lx tx %lx", gDmaPhys, gRingPhys, gTxPhys);

	fw = iwi_fw + 16;
	if ((r = load_firmware(fw, bootsz, &seen)) != 0)
		return -20 + r;
	fw = iwi_fw + 16 + bootsz;
	if ((r = load_ucode(fw, ucodesz)) != 0)
		return -30 + r;
	stop_master();

	csr_write(0x0200, gRingPhys);
	csr_write(0x0204, CMD_RING_COUNT);
	csr_write(CSR_CMD_WIDX, 0);
	for (ac = 0; ac < 4; ac++) {
		csr_write(0x0208 + ac * 8, gRingPhys + TX_RING_OFFSET + ac * 0x4000);
		csr_write(0x020c + ac * 8, TX_RING_COUNT);
		csr_write(0x0f84 + ac * 4, 0);
	}
	for (i = 0; i < RX_COUNT; i++)
		csr_write(0x0500 + i * 4, gRingPhys + RX_BUF_OFFSET + i * RX_BUF_SIZE);
	csr_write(CSR_RX_WIDX, RX_COUNT - 1);
	fw = iwi_fw + 16 + bootsz + ucodesz;
	if ((r = load_firmware(fw, mainsz, &seen)) != 0)
		return -40 + r;
	gUp = 1;
	return 0;
}

/* one received item (copied out); returns its length, 0 if none */
static int
rx_item(u8 *buf)
{
	const u8 *b;
	u32 hw;
	int n;

	if (!gUp)
		return 0;
	ack_intr();
	hw = csr_read(CSR_RX_RIDX);
	if (gRxCur == hw)
		return 0;
	b = gRing + RX_BUF_OFFSET + gRxCur * RX_BUF_SIZE;
	if (b[0] == HDR_TYPE_FRAME)
		n = 4 + 24 + (b[4 + 22] | (b[4 + 23] << 8));
	else if (b[0] == HDR_TYPE_NOTIF)
		n = 4 + 12 + (b[4 + 10] | (b[4 + 11] << 8));
	else
		n = 32;
	if (n > RX_BUF_SIZE)
		n = RX_BUF_SIZE;
	bcopy(b, buf, n);
	gRxCur = (gRxCur + 1) % RX_COUNT;
	csr_write(CSR_RX_WIDX, gRxCur == 0 ? RX_COUNT - 1 : gRxCur - 1);
	return n;
}

/* one 802.11 data frame (header + body, already encrypted if needed) */
static int
tx_frame_once(const u8 *f, int n)
{
	u8 *d;
	int hdrlen, plen, idx, qos;
	u32 pa;

	if (!gUp || n < 24 || n > TX_BUF_SIZE - 64)
		return -1;
	qos = (f[0] & 0x8c) == 0x88;
	hdrlen = qos ? 26 : 24;
	plen = n - hdrlen;
	idx = gTxCur;
	if ((u32)((idx + 1) % TX_RING_COUNT) == csr_read(CSR_TX_RIDX0))
		return -2;
	d = gRing + TX_RING_OFFSET + idx * TX_DESC_SIZE;
	bzero(d, TX_DESC_SIZE);
	d[0] = 0;
	d[2] = HDR_FLAG_IRQ;
	d[12] = 0x0b;
	d[14] = plen & 0xff;
	d[15] = plen >> 8;
	d[17] = 0x20 | ((f[4] & 1) ? 0 : 0x80);
	if (qos)
		d[18] = 0x10;
	bcopy(f, d + 48, hdrlen);
	d[88] = 1;
	pa = gTxPhys + idx * TX_BUF_SIZE;
	d[92] = pa & 0xff; d[93] = (pa >> 8) & 0xff; d[94] = (pa >> 16) & 0xff; d[95] = pa >> 24;
	d[116] = plen & 0xff;
	d[117] = plen >> 8;
	bcopy(f + hdrlen, gTxBuf + idx * TX_BUF_SIZE, plen);
	gTxCur = (idx + 1) % TX_RING_COUNT;
	csr_write(CSR_TX_WIDX0, gTxCur);
	ack_intr();
	return 0;
}

static int
tx_frame(const u8 *frame, int len)
{
	int tries;

	for (tries = 0; tries < 50; tries++) {
		if (tx_frame_once(frame, len) == 0)
			return 0;
		snooze(2000);
	}
	return -1;
}

/* ------------------------------------------------------------------ supplicant (from iwid.c) */

#define CMD_ENABLE		2
#define CMD_SET_CONFIG		6
#define CMD_SET_ESSID		8
#define CMD_SET_MAC_ADDRESS	11
#define CMD_SET_RTS_THRESHOLD	15
#define CMD_SET_FRAG_THRESHOLD	16
#define CMD_SET_POWER_MODE	17
#define CMD_ASSOCIATE		21
#define CMD_SET_RATES		22
#define CMD_SCAN		26
#define CMD_SET_OPTIE		31
#define CMD_DISABLE		33
#define CMD_SET_RANDOM_SEED	34
#define CMD_SET_TX_POWER	35
#define CMD_SET_SENSITIVITY	42

#define ITEM_NOTIF		3
#define ITEM_FRAME		9
#define NOTIF_ASSOCIATION	10
#define NOTIF_AUTHENTICATION	11
#define NOTIF_SCAN_COMPLETE	13

#define MAX_AP			32

static char gSsid[64], gPsk[128];
static u8 gPmk[32], gPtk[48], gGtk[32], gBssid[6];
static int gGtkIdx = 1, gHaveGtk = 0;
static aes_ctx gTkCtx, gGtkCtx;
static u8 gTxPn[6];
static unsigned int gSeq = 0;

typedef struct {
	u8 bssid[6];
	int chan, rssi;
	unsigned int capinfo, intval;
	u8 tstamp[8];
	u8 rates[16];
	int nrates;
	u8 rsn[64];
	int rsnlen;
	char ssid[33];
} ap_t;

static ap_t gAp[MAX_AP];
static int gNumAp = 0;

static void
mac_str(const u8 *m, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 6; i++) {
		out[i * 3] = hex[m[i] >> 4];
		out[i * 3 + 1] = hex[m[i] & 15];
		out[i * 3 + 2] = i < 5 ? ':' : 0;
	}
}

static int
setup_card(void)
{
	static u8 buf[128];
	u32 d32;
	int i, r;
	static const u8 rates[12] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };

	r = fw_cmd(CMD_SET_MAC_ADDRESS, gMac, 6);
	if (r) return r;
	bzero(buf, 20);
	buf[4] = 1; buf[15] = 30; buf[18] = 1;
	if ((r = fw_cmd(CMD_SET_CONFIG, buf, 20))) return r;
	d32 = 0;
	if ((r = fw_cmd(CMD_SET_POWER_MODE, &d32, 4))) return r;
	d32 = 2346;
	if ((r = fw_cmd(CMD_SET_RTS_THRESHOLD, &d32, 4))) return r;
	if ((r = fw_cmd(CMD_SET_FRAG_THRESHOLD, &d32, 4))) return r;
	for (i = 0; i < 2; i++) {
		int c;
		bzero(buf, 76);
		buf[0] = 13;
		buf[1] = (i == 0) ? 2 : 1;
		for (c = 0; c < 13; c++) {
			buf[2 + c * 2] = c + 1;
			buf[3 + c * 2] = 20;
		}
		if ((r = fw_cmd(CMD_SET_TX_POWER, buf, 76))) return r;
	}
	bzero(buf, 16);
	buf[0] = 2; buf[1] = 12; buf[2] = 1;
	bcopy(rates, buf + 4, 12);
	if ((r = fw_cmd(CMD_SET_RATES, buf, 16))) return r;
	bzero(buf, 16);
	buf[2] = 1;
	if ((r = fw_cmd(CMD_SET_RATES, buf, 16))) return r;
	random_bytes((u8 *)&d32, 4);
	if ((r = fw_cmd(CMD_SET_RANDOM_SEED, &d32, 4))) return r;
	return fw_cmd(CMD_ENABLE, 0, 0);
}

static void
note_beacon(const u8 *item, int n)
{
	const u8 *wh = item + 28;
	int len = n - 28, fc = wh[0] & 0xfc, i, j, ssidlen = 0;
	const u8 *ie, *end;
	ap_t *a;

	if (len < 36 || (fc != 0x80 && fc != 0x50))
		return;
	for (j = 0; j < gNumAp; j++)
		if (bcmp(gAp[j].bssid, wh + 16, 6) == 0)
			break;
	if (j == gNumAp) {
		if (gNumAp == MAX_AP)
			return;
		gNumAp++;
		bzero(&gAp[j], sizeof(ap_t));
	}
	a = &gAp[j];
	bcopy(wh + 16, a->bssid, 6);
	a->chan = item[12];
	a->rssi = item[17];
	bcopy(wh + 24, a->tstamp, 8);
	a->intval = wh[24 + 8] | (wh[24 + 9] << 8);
	a->capinfo = wh[24 + 10] | (wh[24 + 11] << 8);
	ie = wh + 24 + 12;
	end = wh + len;
	a->nrates = 0;
	a->rsnlen = 0;
	while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
		if (ie[0] == 0) {
			ssidlen = ie[1] > 32 ? 32 : ie[1];
			for (i = 0; i < ssidlen; i++)
				a->ssid[i] = ie[2 + i];
			a->ssid[ssidlen] = '\0';
		} else if (ie[0] == 3 && ie[1] >= 1) {
			a->chan = ie[2];
		} else if (ie[0] == 1 || ie[0] == 50) {
			for (i = 0; i < ie[1] && a->nrates < 16; i++)
				a->rates[a->nrates++] = ie[2 + i];
		} else if (ie[0] == 48 && ie[1] <= 62) {
			bcopy(ie, a->rsn, 2 + ie[1]);
			a->rsnlen = 2 + ie[1];
		}
		ie += 2 + ie[1];
	}
}

static int
scan_for_aps(void)
{
	static u8 buf[128], item[2048];
	int i, n, done = 0;
	unsigned long long t0;

	gNumAp = 0;
	bzero(buf, 96);
	buf[4] = 0x40 | 13;
	for (i = 1; i <= 13; i++)
		buf[4 + i] = i;
	memset(buf + 4 + 54, 0x33, 27);
	buf[4 + 54 + 27 + 1 + 2 + 4] = 40;
	if (fw_cmd(CMD_SCAN, buf, 96) != 0)
		return -1;
	t0 = now_us();
	while (!done && now_us() - t0 < 6000000ULL) {
		n = rx_item(item);
		if (n == 0) {
			snooze(2000);
			continue;
		}
		if (item[0] == ITEM_FRAME)
			note_beacon(item, n);
		else if (item[0] == ITEM_NOTIF && item[12] == NOTIF_SCAN_COMPLETE)
			done = 1;
	}
	return done ? gNumAp : -2;
}

static const u8 kOurRsn[22] = {
	0x30, 0x14, 0x01, 0x00,
	0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
	0x00, 0x00
};

static int
associate(const ap_t *a)
{
	static u8 buf[128], item[2048];
	static const u8 ours[12] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
	int i, j, n, ssidlen = (int)strlen(gSsid), nr = 0, r;
	unsigned long long t0;
	int authed = 0, assoc = 0;

	bzero(buf, 20);
	buf[4] = 1;
	buf[6] = 1;
	buf[8] = 1;
	buf[11] = 1;
	buf[15] = 30;
	buf[17] = 0;
	buf[18] = 1;
	if ((r = fw_cmd(CMD_SET_CONFIG, buf, 20)) != 0) { say("set config failed %d", r); return -1; }
	if ((r = fw_cmd(CMD_SET_ESSID, gSsid, ssidlen)) != 0) { say("set essid failed %d", r); return -1; }
	bzero(buf, 16);
	buf[0] = 2;
	buf[2] = 0;
	for (i = 0; i < a->nrates && nr < 12; i++) {
		for (j = 0; j < 12; j++) {
			if ((a->rates[i] & 0x7f) == ours[j]) {
				buf[4 + nr++] = a->rates[i];
				break;
			}
		}
	}
	buf[1] = nr;
	if ((r = fw_cmd(CMD_SET_RATES, buf, 16)) != 0) { say("set rates failed %d", r); return -1; }
	{
		int dbm = a->rssi - 112;
		u8 sens[4];

		sens[0] = (u8)(dbm & 0xff);
		sens[1] = (u8)((dbm >> 8) & 0xff);
		sens[2] = sens[3] = 0;
		if ((r = fw_cmd(CMD_SET_SENSITIVITY, sens, 4)) != 0) { say("set sensitivity failed %d", r); return -1; }
	}
	if ((r = fw_cmd(CMD_SET_OPTIE, kOurRsn, sizeof(kOurRsn))) != 0) { say("set optie failed %d", r); return -1; }
	bzero(buf, 40);
	buf[0] = a->chan;
	buf[1] = 0;
	buf[2] = 0;
	buf[4] = 2; buf[5] = 0;
	buf[6] = 0;
	buf[7] = 2;
	bcopy(a->bssid, buf + 8, 6);
	bcopy(a->tstamp, buf + 14, 8);
	buf[22] = 0x01; buf[23] = 0x00;
	buf[24] = 10; buf[25] = 0;
	buf[26] = a->intval & 0xff; buf[27] = a->intval >> 8;
	bcopy(a->bssid, buf + 28, 6);
	if ((r = fw_cmd(CMD_ASSOCIATE, buf, 40)) != 0) { say("associate command failed %d", r); return -1; }
	t0 = now_us();
	while (now_us() - t0 < 8000000ULL) {
		n = rx_item(item);
		if (n == 0) {
			snooze(2000);
			continue;
		}
		if (item[0] == ITEM_NOTIF && item[12] == NOTIF_AUTHENTICATION) {
			say("authentication notification: state %d", item[16]);
			if (item[16] == 9)
				authed = 1;
		} else if (item[0] == ITEM_NOTIF && item[12] == NOTIF_ASSOCIATION) {
			say("association notification: state %d status %d", item[16], item[43] | (item[44] << 8));
			if (item[16] == 12) {
				assoc = 1;
				break;
			}
		}
	}
	if (!assoc)
		say("not associated (authenticated=%d)", authed);
	return assoc ? 0 : -1;
}

/* build an 802.11 data frame (To-DS) carrying an LLC/SNAP payload */
static int
build_data_to_ds(u8 *out, const u8 *da, int protect, const u8 *payload, int plen)
{
	u8 *p = out;
	unsigned int sc;

	p[0] = 0x08;
	p[1] = 0x01 | (protect ? 0x40 : 0);
	p[2] = p[3] = 0;
	bcopy(gBssid, p + 4, 6);
	bcopy(gMac, p + 10, 6);
	bcopy(da, p + 16, 6);
	sc = (gSeq++ & 0xfff) << 4;
	p[22] = sc & 0xff;
	p[23] = sc >> 8;
	if (!protect) {
		bcopy(payload, p + 24, plen);
		return 24 + plen;
	}
	{
		u8 nonce[13], aad[22], mic[8];
		int i;

		for (i = 0; i < 6; i++) {
			if (++gTxPn[i] != 0)
				break;
		}
		p[24] = gTxPn[0]; p[25] = gTxPn[1]; p[26] = 0; p[27] = 0x20;
		p[28] = gTxPn[2]; p[29] = gTxPn[3]; p[30] = gTxPn[4]; p[31] = gTxPn[5];
		nonce[0] = 0;
		bcopy(gMac, nonce + 1, 6);
		for (i = 0; i < 6; i++)
			nonce[7 + i] = gTxPn[5 - i];
		aad[0] = p[0] & 0x8f;
		aad[1] = p[1] & 0xc7;
		bcopy(p + 4, aad + 2, 18);
		aad[20] = p[22] & 0x0f;
		aad[21] = 0;
		bcopy(payload, p + 32, plen);
		ccm_encrypt(&gTkCtx, nonce, aad, 22, p + 32, plen, mic);
		bcopy(mic, p + 32 + plen, 8);
		return 32 + plen + 8;
	}
}

static const u8 kSnapEapol[8] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };

static int
send_eapol(const u8 *eapol, int len)
{
	static u8 payload[512], frame[600];
	int n;

	bcopy(kSnapEapol, payload, 8);
	bcopy(eapol, payload + 8, len);
	n = build_data_to_ds(frame, gBssid, 0, payload, 8 + len);
	return tx_frame(frame, n);
}

static int
wait_eapol(u8 *item, const u8 **eapol, int timeout_ms)
{
	unsigned long long t0 = now_us();
	int n;

	while (now_us() - t0 < (unsigned long long)timeout_ms * 1000) {
		n = rx_item(item);
		if (n == 0) {
			snooze(2000);
			continue;
		}
		if (item[0] == ITEM_FRAME && n >= 28 + 24 + 8 + 4) {
			const u8 *wh = item + 28;
			if ((wh[0] & 0x0c) == 0x08 && !(wh[1] & 0x40) && bcmp(wh + 24, kSnapEapol, 8) == 0
					&& bcmp(wh + 10, gBssid, 6) == 0) {
				*eapol = wh + 24 + 8;
				return n - 28 - 24 - 8;
			}
		} else if (item[0] == ITEM_NOTIF && item[12] == NOTIF_ASSOCIATION) {
			say("association notification during handshake: state %d", item[16]);
		}
	}
	return 0;
}

static void put_be16(u8 *p, unsigned int v) { p[0] = v >> 8; p[1] = v & 0xff; }
static unsigned int get_be16(const u8 *p) { return (p[0] << 8) | p[1]; }

#define K_DESC		4
#define K_INFO		5
#define K_LEN		7
#define K_REPLAY	9
#define K_NONCE		17
#define K_IV		49
#define K_RSC		65
#define K_ID		73
#define K_MIC		81
#define K_DLEN		97
#define K_DATA		99

static void
eapol_mic(const u8 *kck, u8 *frame, int len)
{
	u8 mic[20];

	bzero(frame + K_MIC, 16);
	hmac_sha1(kck, 16, frame, len, mic);
	bcopy(mic, frame + K_MIC, 16);
}

static int
do_handshake(void)
{
	static u8 item[2048], f2[256], f4[128], plain[256], copy[512];
	const u8 *m;
	u8 anonce[32], snonce[32], data[2 * 6 + 2 * 32], replay[8];
	u8 mic_save[16], mic_calc[20];
	int n, len2, tries, info = 0, i, got1 = 0;

	random_bytes(snonce, 32);
	say("waiting for EAPOL message 1 from the AP");
	for (tries = 0; tries < 6 && !got1; tries++) {
		n = wait_eapol(item, &m, 2000);
		if (n == 0)
			continue;
		if (m[1] != 3 || m[K_DESC] != 2)
			continue;
		info = get_be16(m + K_INFO);
		if ((info & 0x0188) == 0x0088) {
			got1 = 1;
			bcopy(m + K_NONCE, anonce, 32);
			bcopy(m + K_REPLAY, replay, 8);
			if ((info & 7) != 2)
				say("note: key descriptor version %d (expected 2)", info & 7);
		}
	}
	if (!got1) {
		say("no EAPOL message 1 received");
		return -1;
	}
	say("message 1 received (key info 0x%04x)", info);
	if (bcmp(gBssid, gMac, 6) < 0) {
		bcopy(gBssid, data, 6);
		bcopy(gMac, data + 6, 6);
	} else {
		bcopy(gMac, data, 6);
		bcopy(gBssid, data + 6, 6);
	}
	if (bcmp(anonce, snonce, 32) < 0) {
		bcopy(anonce, data + 12, 32);
		bcopy(snonce, data + 44, 32);
	} else {
		bcopy(snonce, data + 12, 32);
		bcopy(anonce, data + 44, 32);
	}
	wpa_prf(gPmk, 32, "Pairwise key expansion", data, sizeof(data), gPtk, 48);

	bzero(f2, sizeof(f2));
	f2[0] = 2;
	f2[1] = 3;
	f2[K_DESC] = 2;
	put_be16(f2 + K_INFO, 0x010a);
	put_be16(f2 + K_LEN, 0);
	bcopy(replay, f2 + K_REPLAY, 8);
	bcopy(snonce, f2 + K_NONCE, 32);
	put_be16(f2 + K_DLEN, sizeof(kOurRsn));
	bcopy(kOurRsn, f2 + K_DATA, sizeof(kOurRsn));
	len2 = K_DATA + sizeof(kOurRsn);
	put_be16(f2 + 2, len2 - 4);
	eapol_mic(gPtk, f2, len2);
	if (send_eapol(f2, len2) != 0) {
		say("could not transmit message 2");
		return -1;
	}
	say("message 2 sent");

	for (tries = 0; tries < 8; tries++) {
		int dlen, ok = 0, elen;

		n = wait_eapol(item, &m, 2000);
		if (n == 0) {
			say("timeout waiting for message 3 (the AP may have rejected message 2: wrong password?)");
			continue;
		}
		if (m[1] != 3 || m[K_DESC] != 2)
			continue;
		info = get_be16(m + K_INFO);
		if ((info & 0x0188) == 0x0088 && !(info & 0x100)) {
			send_eapol(f2, len2);
			continue;
		}
		if ((info & 0x01c8) != 0x01c8 && (info & 0x0148) != 0x0148)
			continue;
		elen = 4 + get_be16(m + 2);
		if (elen > (int)sizeof(copy) || elen > n)
			continue;
		bcopy(m, copy, elen);
		bcopy(copy + K_MIC, mic_save, 16);
		bzero(copy + K_MIC, 16);
		hmac_sha1(gPtk, 16, copy, elen, mic_calc);
		ok = bcmp(mic_save, mic_calc, 16) == 0;
		if (!ok) {
			say("message 3 MIC does not verify (wrong passphrase?)");
			return -2;
		}
		say("message 3 received, MIC verified (key info 0x%04x)", info);
		bcopy(m + K_REPLAY, replay, 8);
		dlen = get_be16(m + K_DLEN);
		if (dlen > 0 && (info & 0x1000)) {
			if (dlen < 24 || dlen > (int)sizeof(plain) + 8 || aes_unwrap(gPtk + 16, m + K_DATA, dlen, plain) != 0) {
				say("key data unwrap failed");
				return -3;
			}
			dlen -= 8;
		} else {
			bcopy(m + K_DATA, plain, dlen);
		}
		gHaveGtk = 0;
		for (i = 0; i + 2 <= dlen; ) {
			int l = plain[i + 1];
			if (plain[i] == 0xdd && l >= 22 && plain[i + 2] == 0x00 && plain[i + 3] == 0x0f
					&& plain[i + 4] == 0xac && plain[i + 5] == 0x01) {
				int gl = l - 6;
				gGtkIdx = plain[i + 6] & 3;
				if (gl > 32)
					gl = 32;
				bcopy(plain + i + 8, gGtk, gl);
				if (gl == 16)
					gHaveGtk = 1;
				else
					say("group key is %d bytes (TKIP?), not CCMP: group traffic unsupported", gl);
			}
			if (plain[i] == 0xdd && l == 0)
				break;
			i += 2 + l;
		}
		bzero(f4, sizeof(f4));
		f4[0] = 2; f4[1] = 3; f4[K_DESC] = 2;
		put_be16(f4 + K_INFO, 0x030a);
		bcopy(replay, f4 + K_REPLAY, 8);
		put_be16(f4 + K_DLEN, 0);
		put_be16(f4 + 2, K_DATA - 4);
		eapol_mic(gPtk, f4, K_DATA);
		if (send_eapol(f4, K_DATA) != 0) {
			say("could not transmit message 4");
			return -1;
		}
		say("message 4 sent: handshake complete%s", gHaveGtk ? ", GTK installed" : " (no usable GTK)");
		aes_set_key(&gTkCtx, gPtk + 32);
		if (gHaveGtk)
			aes_set_key(&gGtkCtx, gGtk);
		bzero(gTxPn, 6);
		return 0;
	}
	return -1;
}

static int
send_eapol_enc(const u8 *eapol, int len)
{
	static u8 payload[512], frame[640];
	int n;

	bcopy(kSnapEapol, payload, 8);
	bcopy(eapol, payload + 8, len);
	n = build_data_to_ds(frame, gBssid, 1, payload, 8 + len);
	return tx_frame(frame, n);
}

static void
group_rekey(const u8 *m, int n)
{
	static u8 plain[256], copy[512], f2[128];
	u8 mic_calc[20], replay[8];
	int info, dlen, elen, i;

	if (n < K_DATA || m[1] != 3 || m[K_DESC] != 2)
		return;
	info = get_be16(m + K_INFO);
	if (info & 0x08) {
		say("EAPOL pairwise message after the handshake: key info 0x%04x", info);
		return;
	}
	elen = 4 + get_be16(m + 2);
	if (elen > (int)sizeof(copy) || elen > n)
		return;
	bcopy(m, copy, elen);
	bzero(copy + K_MIC, 16);
	hmac_sha1(gPtk, 16, copy, elen, mic_calc);
	if (bcmp(m + K_MIC, mic_calc, 16) != 0) {
		say("group key message: bad MIC, ignored");
		return;
	}
	bcopy(m + K_REPLAY, replay, 8);
	dlen = get_be16(m + K_DLEN);
	if (dlen > 0 && (info & 0x1000)) {
		if (dlen < 24 || dlen > (int)sizeof(plain) + 8 || aes_unwrap(gPtk + 16, m + K_DATA, dlen, plain) != 0) {
			say("group key unwrap failed");
			return;
		}
		dlen -= 8;
	} else {
		return;
	}
	for (i = 0; i + 2 <= dlen; ) {
		int l = plain[i + 1];
		if (plain[i] == 0xdd && l >= 22 && plain[i + 2] == 0x00 && plain[i + 3] == 0x0f
				&& plain[i + 4] == 0xac && plain[i + 5] == 0x01 && l - 6 == 16) {
			gGtkIdx = plain[i + 6] & 3;
			bcopy(plain + i + 8, gGtk, 16);
			aes_set_key(&gGtkCtx, gGtk);
			gHaveGtk = 1;
		}
		if (plain[i] == 0xdd && l == 0)
			break;
		i += 2 + l;
	}
	bzero(f2, sizeof(f2));
	f2[0] = 2; f2[1] = 3; f2[K_DESC] = 2;
	put_be16(f2 + K_INFO, 0x0302);
	bcopy(replay, f2 + K_REPLAY, 8);
	put_be16(f2 + 2, K_DATA - 4);
	eapol_mic(gPtk, f2, K_DATA);
	send_eapol_enc(f2, K_DATA);
	say("group key refreshed");
}

/* decrypt a received 802.11 frame to an Ethernet frame; returns its length or -1 */
static int
rx_to_ethernet(const u8 *item, int n, u8 *out)
{
	const u8 *wh = item + 28;
	int len = n - 28, hdrlen = 24, plen, tid = 0, bcast;
	u8 nonce[13], aad[24], mic[8], pn[6];
	static u8 body[2048];
	const aes_ctx *ctx;
	int aadlen;

	if (len < 24 + 8 + 8 || (wh[0] & 0x0c) != 0x08)
		return -1;
	if ((wh[0] & 0x80)) {
		hdrlen = 26;
		tid = wh[24] & 0x0f;
	}
	if ((wh[1] & 0x03) != 0x02)
		return -1;
	if (!(wh[1] & 0x40))
		return -1;
	if (len < hdrlen + 8 + 8)
		return -1;
	bcast = wh[4] & 1;
	if (!(wh[hdrlen + 3] & 0x20))
		return -1;
	ctx = bcast ? &gGtkCtx : &gTkCtx;
	if (bcast && !gHaveGtk)
		return -1;
	pn[0] = wh[hdrlen + 7]; pn[1] = wh[hdrlen + 6]; pn[2] = wh[hdrlen + 5];
	pn[3] = wh[hdrlen + 4]; pn[4] = wh[hdrlen + 1]; pn[5] = wh[hdrlen + 0];
	nonce[0] = tid;
	bcopy(wh + 10, nonce + 1, 6);
	bcopy(pn, nonce + 7, 6);
	aad[0] = wh[0] & 0x8f;
	aad[1] = wh[1] & 0xc7;
	bcopy(wh + 4, aad + 2, 18);
	aad[20] = wh[22] & 0x0f;
	aad[21] = 0;
	aadlen = 22;
	if (hdrlen == 26) {
		aad[22] = tid;
		aad[23] = 0;
		aadlen = 24;
	}
	plen = len - hdrlen - 8 - 8;
	bcopy(wh + len - 8, mic, 8);
	bcopy(wh + hdrlen + 8, body, plen);
	if (ccm_decrypt(ctx, nonce, aad, aadlen, body, plen, mic) != 0)
		return -1;
	if (plen < 8 || body[0] != 0xaa || body[1] != 0xaa || body[2] != 0x03)
		return -1;
	bcopy(wh + 4, out, 6);
	bcopy(wh + 16, out + 6, 6);
	bcopy(body + 6, out + 12, plen - 6);
	return 12 + plen - 6;
}

static int
send_ethernet(const u8 *eth, int len)
{
	static u8 payload[1600], frame[1700];
	int n;

	if (len < 14 || len > 1514)
		return -1;
	bcopy(kSnapEapol, payload, 6);
	bcopy(eth + 12, payload + 6, len - 12);
	n = build_data_to_ds(frame, eth, 1, payload, 6 + len - 12);
	return tx_frame(frame, n);
}


/* ------------------------------------------------------------------ DHCP client (from iwid.c), runs on the air link;
   the result is published in net.iwi.dhcp and applied to en1 by iwi-connect */

static u8 gIp[4], gMask[4], gRouter[4], gDns[4], gServer[4];
static u32 gLease = 0;
static unsigned long long gLeaseStart = 0;
static void set_state(const char *s);
#define set_state_dhcp() set_state("dhcp")
static char gDhcpText[160];

static unsigned int
ip_checksum(const u8 *p, int n)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i + 1 < n; i += 2)
		sum += (p[i] << 8) | p[i + 1];
	if (n & 1)
		sum += p[n - 1] << 8;
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (~sum) & 0xffff;
}

static int
dhcp_send(int msgtype, u32 xid, const u8 *req_ip, const u8 *server_id, const u8 *ciaddr)
{
	static u8 f[600];
	u8 *ip = f + 14, *udp = ip + 20, *b = udp + 8, *o;
	int blen, ulen, total;

	bzero(f, sizeof(f));
	memset(f, 0xff, 6);
	bcopy(gMac, f + 6, 6);
	f[12] = 0x08; f[13] = 0x00;
	b[0] = 1; b[1] = 1; b[2] = 6; b[3] = 0;
	b[4] = xid >> 24; b[5] = xid >> 16; b[6] = xid >> 8; b[7] = xid;
	b[10] = 0x80;
	if (ciaddr)
		bcopy(ciaddr, b + 12, 4);
	bcopy(gMac, b + 28, 6);
	b[236] = 0x63; b[237] = 0x82; b[238] = 0x53; b[239] = 0x63;
	o = b + 240;
	*o++ = 53; *o++ = 1; *o++ = msgtype;
	*o++ = 61; *o++ = 7; *o++ = 1; bcopy(gMac, o, 6); o += 6;
	if (req_ip) { *o++ = 50; *o++ = 4; bcopy(req_ip, o, 4); o += 4; }
	if (server_id) { *o++ = 54; *o++ = 4; bcopy(server_id, o, 4); o += 4; }
	*o++ = 12; *o++ = 8; bcopy("dell510m", o, 8); o += 8;
	*o++ = 55; *o++ = 5; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15; *o++ = 51;
	*o++ = 255;
	blen = (int)(o - b);
	if (blen < 300)
		blen = 300;
	ulen = 8 + blen;
	udp[0] = 0; udp[1] = 68; udp[2] = 0; udp[3] = 67;
	udp[4] = ulen >> 8; udp[5] = ulen & 0xff;
	total = 20 + ulen;
	ip[0] = 0x45; ip[2] = total >> 8; ip[3] = total & 0xff;
	ip[4] = 0; ip[5] = (u8)(xid & 0xff);
	ip[8] = 64; ip[9] = 17;
	memset(ip + 16, 0xff, 4);
	{
		unsigned int ck = ip_checksum(ip, 20);
		ip[10] = ck >> 8; ip[11] = ck & 0xff;
	}
	return send_ethernet(f, 14 + total);
}

static int
dhcp_parse(const u8 *eth, int len, u32 xid, u8 *yiaddr, u8 *mask, u8 *router, u8 *dns, u8 *server, u32 *lease)
{
	const u8 *ip, *udp, *b, *o, *end;
	int ihl, type = 0;

	if (len < 14 + 20 + 8 + 240 || eth[12] != 0x08 || eth[13] != 0x00)
		return 0;
	ip = eth + 14;
	ihl = (ip[0] & 0x0f) * 4;
	if ((ip[0] >> 4) != 4 || ip[9] != 17 || len < 14 + ihl + 8 + 240)
		return 0;
	udp = ip + ihl;
	if (udp[2] != 0 || udp[3] != 68)
		return 0;
	b = udp + 8;
	if (b[0] != 2 || b[236] != 0x63 || b[237] != 0x82 || b[238] != 0x53 || b[239] != 0x63)
		return 0;
	if (b[4] != (u8)(xid >> 24) || b[5] != (u8)(xid >> 16) || b[6] != (u8)(xid >> 8) || b[7] != (u8)xid)
		return 0;
	bcopy(b + 16, yiaddr, 4);
	o = b + 240;
	end = eth + len;
	while (o + 2 <= end && *o != 255) {
		if (*o == 0) { o++; continue; }
		if (o + 2 + o[1] > end)
			break;
		switch (*o) {
		case 53: type = o[2]; break;
		case 1: if (o[1] >= 4) bcopy(o + 2, mask, 4); break;
		case 3: if (o[1] >= 4) bcopy(o + 2, router, 4); break;
		case 6: if (o[1] >= 4) bcopy(o + 2, dns, 4); break;
		case 54: if (o[1] >= 4) bcopy(o + 2, server, 4); break;
		case 51: if (o[1] >= 4) *lease = ((u32)o[2] << 24) | (o[3] << 16) | (o[4] << 8) | o[5]; break;
		}
		o += 2 + o[1];
	}
	return type;
}

static int
dhcp_wait(int want, u32 xid, int ms, u8 *yi, u8 *mask, u8 *router, u8 *dns, u8 *server, u32 *lease)
{
	static u8 item[2048], eth[2048];
	unsigned long long t0 = now_us();
	int n, m, t;

	while (now_us() - t0 < (unsigned long long)ms * 1000) {
		n = rx_item(item);
		if (n == 0) {
			snooze(2000);
			continue;
		}
		if (item[0] != ITEM_FRAME)
			continue;
		m = rx_to_ethernet(item, n, eth);
		if (m < 0)
			continue;
		t = dhcp_parse(eth, m, xid, yi, mask, router, dns, server, lease);
		if (t == want)
			return 1;
		if (t == 6)
			return -1;
	}
	return 0;
}

static void
dhcp_publish(void)
{
	snprintf(gDhcpText, sizeof(gDhcpText), "ip=%d.%d.%d.%d mask=%d.%d.%d.%d router=%d.%d.%d.%d dns=%d.%d.%d.%d lease=%lu",
	    gIp[0], gIp[1], gIp[2], gIp[3], gMask[0], gMask[1], gMask[2], gMask[3],
	    gRouter[0], gRouter[1], gRouter[2], gRouter[3], gDns[0], gDns[1], gDns[2], gDns[3], (unsigned long)gLease);
}

static int
dhcp_acquire(void)
{
	u8 yi[4], mask[4], router[4], dns[4], server[4];
	u32 lease = 0, xid;
	int attempt;

	set_state_dhcp();
	for (attempt = 0; attempt < 4; attempt++) {
		random_bytes((u8 *)&xid, 4);
		bzero(mask, 4); bzero(router, 4); bzero(dns, 4); bzero(server, 4);
		say("DHCP: discover (try %d)", attempt + 1);
		if (dhcp_send(1, xid, 0, 0, 0) != 0)
			continue;
		if (dhcp_wait(2, xid, 4000, yi, mask, router, dns, server, &lease) != 1)
			continue;
		say("DHCP: offer %d.%d.%d.%d from %d.%d.%d.%d", yi[0], yi[1], yi[2], yi[3], server[0], server[1], server[2], server[3]);
		dhcp_send(3, xid, yi, server, 0);
		if (dhcp_wait(5, xid, 4000, yi, mask, router, dns, server, &lease) != 1)
			continue;
		bcopy(yi, gIp, 4); bcopy(mask, gMask, 4); bcopy(router, gRouter, 4);
		bcopy(dns, gDns, 4); bcopy(server, gServer, 4);
		gLease = lease ? lease : 3600;
		gLeaseStart = now_us();
		dhcp_publish();
		say("DHCP: %s", gDhcpText);
		return 0;
	}
	say("DHCP: no lease obtained");
	return -1;
}

static void
dhcp_renew(void)
{
	u8 yi[4], mask[4], router[4], dns[4], server[4];
	u32 lease = 0, xid;

	random_bytes((u8 *)&xid, 4);
	bcopy(gMask, mask, 4); bcopy(gRouter, router, 4); bcopy(gDns, dns, 4); bcopy(gServer, server, 4);
	dhcp_send(3, xid, gIp, gServer, gIp);
	if (dhcp_wait(5, xid, 4000, yi, mask, router, dns, server, &lease) == 1) {
		gLease = lease ? lease : gLease;
		gLeaseStart = now_us();
		dhcp_publish();
		say("DHCP: lease renewed for %lu s", (unsigned long)gLease);
	} else {
		say("DHCP: renew got no answer yet");
		gLeaseStart += 30000000ULL;
	}
}

/* ------------------------------------------------------------------ network interface (ifnet) */

#define NETQ 64
#define NETFRAME 1536
struct netbuf { int len; u8 data[NETFRAME]; };
static struct netbuf gOutq[NETQ];
static int gOutHead = 0, gOutCount = 0;
static lck_mtx_t *gQLock;
static ifnet_t gIfp = 0;
static int gIfAttached = 0;
static unsigned long gRxPk = 0, gTxPk = 0, gDrops = 0;
static volatile int gInMask = 3;		/* frames handed to the stack: 1 ARP, 2 IPv4, 4 IPv6, 8 everything else, 16 multicast.
						   Multicast/IPv6 delivery froze the machine (cause not found), so the default is ARP + IPv4 unicast/broadcast */
static volatile int gNoInput = 0;		/* 1 = do not hand received frames to the stack (cmd 14/15) */

static errno_t
iwi_if_output(ifnet_t ifp, mbuf_t m)
{
	int len = mbuf_pkthdr_len(m), slot;

	if (len >= 14 && len <= 1514) {
		lck_mtx_lock(gQLock);
		if (gOutCount < NETQ) {
			slot = (gOutHead + gOutCount) % NETQ;
			mbuf_copydata(m, 0, len, gOutq[slot].data);
			gOutq[slot].len = len;
			gOutCount++;
		} else
			gDrops++;
		lck_mtx_unlock(gQLock);
	}
	mbuf_freem(m);
	return 0;
}

static errno_t
iwi_if_ioctl(ifnet_t ifp, u_int32_t cmd, void *data)
{
	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifnet_flags(ifp) & IFF_UP)
			ifnet_set_flags(ifp, IFF_RUNNING, IFF_RUNNING);
		else
			ifnet_set_flags(ifp, 0, IFF_RUNNING);
		return 0;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		return 0;
	}
	return EOPNOTSUPP;
}

static void
if_input(const u8 *eth, int len)
{
	mbuf_t m;
	struct ifnet_stat_increment_param st;

	if (!gIfAttached || gNoInput)
		return;
	{
		int t = (eth[12] << 8) | eth[13], bit = t == 0x0806 ? 1 : t == 0x0800 ? 2 : t == 0x86dd ? 4 : 8;

		if (!(gInMask & bit))
			return;
		/* multicast frames (not broadcast) only when bit 16 is set */
		if ((eth[0] & 1) && !(eth[0] == 0xff && eth[1] == 0xff && eth[2] == 0xff && eth[3] == 0xff && eth[4] == 0xff && eth[5] == 0xff) && !(gInMask & 16))
			return;
	}
	/* one contiguous buffer: mbuf_allocpacket() may return a chain of small mbufs (88 + 232 bytes ...) for
	   packets between ~90 and ~320 bytes, and copying the whole frame into the first one overran it */
	if (mbuf_gethdr(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) != 0)
		return;
	mbuf_mclget(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m);
	if (mbuf_trailingspace(m) < (size_t)len) {
		mbuf_freem(m);
		return;
	}
	bcopy(eth, mbuf_data(m), len);
	mbuf_setlen(m, len);
	mbuf_pkthdr_setlen(m, len);
	mbuf_pkthdr_setrcvif(m, gIfp);
	mbuf_pkthdr_setheader(m, mbuf_data(m));
	mbuf_adj(m, 14);
	bzero(&st, sizeof(st));
	st.packets_in = 1;
	st.bytes_in = len;
	ifnet_input(gIfp, m, &st);
}

static int
if_attach(void)
{
	struct ifnet_init_params ip;
	struct sockaddr_dl sdl;
	static u8 bcast[6] = { 255, 255, 255, 255, 255, 255 };
	errno_t e;

	if (gIfAttached)
		return 0;
	bzero(&ip, sizeof(ip));
	ip.name = "en";
	ip.unit = 1;
	ip.family = IFNET_FAMILY_ETHERNET;
	ip.type = IFT_ETHER;
	ip.output = iwi_if_output;
	ip.demux = ether_demux;
	ip.add_proto = ether_add_proto;
	ip.del_proto = ether_del_proto;
	ip.check_multi = ether_check_multi;
	ip.framer = ether_frameout;
	ip.ioctl = iwi_if_ioctl;
	ip.broadcast_addr = bcast;
	ip.broadcast_len = 6;
	if ((e = ifnet_allocate(&ip, &gIfp)) != 0) {
		say("ifnet_allocate failed %d", e);
		return -1;
	}
	ifnet_set_mtu(gIfp, 1500);
	ifnet_set_flags(gIfp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST, 0xffff);
	ifnet_set_addrlen(gIfp, 6);
	ifnet_set_hdrlen(gIfp, 14);
	ifnet_set_baudrate(gIfp, 54000000);
	bzero(&sdl, sizeof(sdl));
	sdl.sdl_len = sizeof(sdl);
	sdl.sdl_family = AF_LINK;
	sdl.sdl_type = IFT_ETHER;
	sdl.sdl_alen = 6;
	bcopy(gMac, LLADDR(&sdl), 6);
	if ((e = ifnet_attach(gIfp, &sdl)) != 0) {
		say("ifnet_attach failed %d", e);
		return -2;
	}
	gIfAttached = 1;
	say("interface en1 attached");
	return 0;
}

/* ------------------------------------------------------------------ control: sysctl net.iwi.* + worker thread */

enum { C_NONE, C_RESET, C_START, C_SCAN, C_CONNECT, C_DOWN, C_AUTO = 9 };
static volatile int gCmd = 0;
static volatile int gQuit = 0;
static volatile int gLinked = 0;
static volatile int gAuto = 0;
static char gState[96] = "idle";
static char gScanText[1536];
static char gStatusText[512];
static int gThreadRunning = 0;

static void set_state(const char *s) { strncpy(gState, s, sizeof(gState) - 1); gState[sizeof(gState) - 1] = 0; }

static int
card_start(void)
{
	int r;
	char b[20];

	set_state("starting card");
	if ((r = do_up()) != 0) {
		say("card start failed, code %d", r);
		set_state("card start failed");
		return -1;
	}
	mac_str(gMac, b);
	say("card is up, MAC %s", b);
	if ((r = setup_card()) != 0) {
		say("card setup failed (%d)", r);
		set_state("card setup failed");
		return -1;
	}
	set_state("card up");
	return 0;
}

static void
build_scan_text(void)
{
	int i, n = 0, k;
	char b[20];

	gScanText[0] = 0;
	for (i = 0; i < gNumAp && n < (int)sizeof(gScanText) - 96; i++) {
		mac_str(gAp[i].bssid, b);
		k = snprintf(gScanText + n, sizeof(gScanText) - n, "ch%2d %4d dBm %s %s%s\n", gAp[i].chan,
		    gAp[i].rssi - 112, b, gAp[i].ssid, gAp[i].rsnlen ? " [RSN]" : "");
		if (k > 0) n += k;
	}
}

static int
do_scan(void)
{
	int n;

	set_state("scanning");
	n = scan_for_aps();
	say("scan: %d access points", n);
	build_scan_text();
	set_state(n >= 0 ? "scan done" : "scan failed");
	return n;
}

static int
connect_once(void)
{
	u8 order[MAX_AP];
	int i, j, n, ncand = 0;
	char b[20];

	if (gSsid[0] == 0 || strlen(gPsk) < 8) {
		say("set net.iwi.ssid and net.iwi.psk first");
		set_state("no ssid/psk");
		return -1;
	}
	n = do_scan();
	for (i = 0; i < gNumAp; i++)
		if (strcmp(gAp[i].ssid, gSsid) == 0 && gAp[i].rsnlen)
			order[ncand++] = i;
	if (ncand == 0) {
		say("no access point called '%s' with WPA2 found", gSsid);
		set_state("ssid not found");
		return -1;
	}
	for (i = 0; i < ncand; i++)
		for (j = i + 1; j < ncand; j++)
			if (gAp[order[j]].rssi > gAp[order[i]].rssi) {
				u8 t = order[i]; order[i] = order[j]; order[j] = t;
			}
	set_state("deriving key");
	pbkdf2_sha1(gPsk, (const u8 *)gSsid, (int)strlen(gSsid), 4096, gPmk, 32);
	for (i = 0; i < ncand; i++) {
		ap_t *a = &gAp[order[i]];
		mac_str(a->bssid, b);
		say("trying %s on channel %d", b, a->chan);
		bcopy(a->bssid, gBssid, 6);
		set_state("associating");
		if (associate(a) != 0)
			continue;
		set_state("handshake");
		if (do_handshake() != 0)
			continue;
		set_state("connected");
		return 0;
	}
	say("could not complete the WPA2 connection");
	set_state("connect failed");
	return -1;
}

/* data path: runs from the worker thread while linked.  returns <0 when the link is lost */
static int
bridge_step(void)
{
	static u8 item[2048], eth[2048], out[2048];
	static unsigned long long lastitem = 0;
	int n, m, busy = 0, olen = 0;

	if (lastitem == 0)
		lastitem = now_us();
	{
		static unsigned long long lastdhcp = 0;

		if (!gLease && now_us() - lastdhcp > 8000000ULL) {	/* no lease yet (the first attempts right after association can be lost) */
			lastdhcp = now_us();
			dhcp_acquire();
		}
	}
	if (gLease && now_us() - gLeaseStart > (unsigned long long)(gLease / 2) * 1000000ULL)
		dhcp_renew();
	n = rx_item(item);
	if (n > 0) {
		busy = 1;
		lastitem = now_us();
		if (item[0] == ITEM_FRAME) {
			m = rx_to_ethernet(item, n, eth);
			if (m > 0)
				crumb("rx %d %02x%02x", m, eth[12], eth[13]);
			if (m > 0 && bcmp(eth + 6, gMac, 6) != 0) {
				if (eth[12] == 0x88 && eth[13] == 0x8e)
					group_rekey(eth + 14, m - 14);
				else {
					if_input(eth, m);
					gRxPk++;
				}
			}
		} else if (item[0] == ITEM_NOTIF && item[12] == NOTIF_ASSOCIATION && item[16] == 0) {
			say("deassociated by the AP");
			lastitem = 0;
			return -1;
		} else if (item[0] == ITEM_NOTIF && item[12] == NOTIF_AUTHENTICATION && item[16] == 0) {
			say("deauthenticated by the AP");
			lastitem = 0;
			return -1;
		}
	}
	lck_mtx_lock(gQLock);
	if (gOutCount > 0) {
		bcopy(gOutq[gOutHead].data, out, gOutq[gOutHead].len);
		olen = gOutq[gOutHead].len;
		gOutHead = (gOutHead + 1) % NETQ;
		gOutCount--;
	}
	lck_mtx_unlock(gQLock);
	if (olen >= 14) {
		busy = 1;
		crumb("tx %d %02x%02x", olen, out[12], out[13]);
		if (send_ethernet(out, olen) == 0)
			gTxPk++;
		crumb("tx done");
	}
	if (now_us() - lastitem > 10000000ULL) {
		say("the card has been silent for 10 s");
		crumb("silent");
		lastitem = 0;
		return -2;
	}
	if (!busy)
		snooze(2000);
	return 0;
}

static void
run_cmd(int cmd)
{
	int r;

	switch (cmd) {
	case C_RESET:
		r = hw_reset();
		say("reset: %d", r);
		if (r == 0) {
			char b[20];
			mac_str(gMac, b);
			say("MAC %s", b);
		}
		break;
	case C_START:
		card_start();
		break;
	case 8: {		/* self test of the stack path: inject an ARP request for 10.99.0.2 */
		static const u8 arp[60] = {
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01, 0x08, 0x06,
			0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01, 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01, 10, 99, 0, 1,
			0, 0, 0, 0, 0, 0, 10, 99, 0, 2 };
		say("inject: ARP request into the stack (attached=%d outq=%d)", gIfAttached, gOutCount);
		if_input(arp, 60);
		IOSleep(500);
		say("inject: outq now %d", gOutCount);
		if (gOutCount > 0)
			say("inject: first queued frame: len %d type %02x%02x dst %02x:%02x:%02x:%02x:%02x:%02x", gOutq[gOutHead].len,
			    gOutq[gOutHead].data[12], gOutq[gOutHead].data[13], gOutq[gOutHead].data[0], gOutq[gOutHead].data[1],
			    gOutq[gOutHead].data[2], gOutq[gOutHead].data[3], gOutq[gOutHead].data[4], gOutq[gOutHead].data[5]);
		break;
	}
	case 12:
		crumb("dhcp start");
		dhcp_acquire();
		crumb("dhcp done");
		break;
	case 16: gInMask = 3; say("input mask ARP+IPv4"); break;
	case 19: gInMask |= 16; say("input mask +multicast"); break;
	case 17: gInMask = 7; say("input mask ARP+IPv4+IPv6"); break;
	case 18: gInMask = 15; say("input mask everything"); break;
	case 20: gIOLog = 0; break;
	case 21: gIOLog = 1; break;
	case 22:
	case 23: {		/* raw driver flood test, no network stack involved: broadcast ARP requests for the router */
		static u8 arpf[60];
		static u8 item[2048];
		int i, n = (cmd == 22) ? 5000 : 1000, sent = 0;

		bzero(arpf, sizeof(arpf));
		memset(arpf, 0xff, 6);
		bcopy(gMac, arpf + 6, 6);
		arpf[12] = 0x08; arpf[13] = 0x06;
		arpf[15] = 1; arpf[16] = 0x08; arpf[18] = 6; arpf[19] = 4; arpf[21] = 1;
		bcopy(gMac, arpf + 22, 6);
		bcopy(gIp, arpf + 28, 4);
		bcopy(gRouter, arpf + 38, 4);
		for (i = 0; i < 600; i++) {
			crumb("flood %d", i);
			if (send_ethernet(arpf, 42) == 0)
				sent++;
			while (rx_item(item) > 0)
				;
			snooze(cmd == 22 ? 5000 : 1000);
		}
		say("flood test: %d frames sent", sent);
		break;
	}
	case 14:
		gNoInput = 1;
		say("stack input off");
		break;
	case 15:
		gNoInput = 0;
		say("stack input ON");
		break;
	case 13: {
		u32 t = csr_read(CSR_CTL);
		csr_write(CSR_CTL, t & ~2u);
		say("ctl %08x -> %08x (standby off)", t, csr_read(CSR_CTL));
		break;
	}
	case 11: {		/* crypto self-test with published vectors */
		static const u8 k16[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
		static const u8 pt[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
		static const u8 aes_ct[16] = { 0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a };
		static const u8 wrapped[24] = { 0x1f, 0xa6, 0x8b, 0x0a, 0x81, 0x12, 0xb4, 0x47, 0xae, 0xf3, 0x4b, 0xd8, 0xfb, 0x5a, 0x7b, 0x82,
		    0x9d, 0x3e, 0x86, 0x23, 0x71, 0xd2, 0xcf, 0xe5 };
		static const u8 pmk_ref[8] = { 0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef };
		aes_ctx c;
		u8 out[32], pmk[32], data[40], nonce[13], aad[22], mic[8], mic2[8];
		int i;

		aes_set_key(&c, k16);
		aes_encrypt(&c, pt, out);
		say("selftest AES-128 (FIPS-197): %s", bcmp(out, aes_ct, 16) == 0 ? "ok" : "FAIL");
		say("selftest AES key unwrap (RFC 3394): %s", (aes_unwrap(k16, wrapped, 24, out) == 0 && bcmp(out, pt, 16) == 0) ? "ok" : "FAIL");
		pbkdf2_sha1("password", (const u8 *)"IEEE", 4, 4096, pmk, 32);
		say("selftest PBKDF2 (802.11i vector): %s", bcmp(pmk, pmk_ref, 8) == 0 ? "ok" : "FAIL");
		for (i = 0; i < 40; i++) data[i] = i * 7;
		for (i = 0; i < 13; i++) nonce[i] = i + 1;
		for (i = 0; i < 22; i++) aad[i] = 0x40 + i;
		bcopy(data, out, 32);
		ccm_encrypt(&c, nonce, aad, 22, out, 32, mic);
		bcopy(mic, mic2, 8);
		say("selftest CCM roundtrip: %s", (ccm_decrypt(&c, nonce, aad, 22, out, 32, mic2) == 0 && bcmp(out, data, 32) == 0) ? "ok" : "FAIL");
		break;
	}
	case 7:
		if (gMacKnown || hw_reset() == 0)
			if_attach();
		break;
	case C_SCAN:
		if (!gUp && card_start() != 0)
			break;
		do_scan();
		break;
	case C_CONNECT:
	case C_AUTO:
		gAuto = (cmd == C_AUTO);
		gLinked = 0;
		gLease = 0;
		gDhcpText[0] = 0;
		if (!gUp && card_start() != 0)
			break;
		if (connect_once() == 0) {
			if (if_attach() != 0)
				break;
			ifnet_set_flags(gIfp, IFF_RUNNING, IFF_RUNNING);
			gLinked = 1;
			say("link is up: en1 is live");
			crumb("linked");
			dhcp_acquire();
			set_state("connected");
		}
		break;
	case C_DOWN:
		gAuto = 0;
		gLinked = 0;
		if (gUp) {
			fw_cmd(CMD_DISABLE, 0, 0);
			csr_write(CSR_INTR_MASK, 0);
			stop_master();
			gUp = 0;
		}
		if (gIfAttached)
			ifnet_set_flags(gIfp, 0, IFF_RUNNING);
		set_state("down");
		break;
	}
}

static void
worker(void *arg)
{
	int cmd, r, tries = 0;
	unsigned long long retry = 0;

	gThreadRunning = 1;
	say("worker running");
	while (!gQuit) {
		gBeat++;
		cmd = gCmd;
		if (cmd) {
			gCmd = 0;
			run_cmd(cmd);
			if (gAuto && gLinked)
				tries = 0;
			else if (gAuto && !gCmd) {
				if (++tries > 20) {
					say("giving up after %d attempts (run sysctl -w net.iwi.cmd=9 to try again)", tries - 1);
					gAuto = 0;
					set_state("gave up");
				} else
					retry = now_us() + 20000000ULL;	/* AP not up yet, out of range, wrong key ... */
			}
			continue;
		}
		if (gLinked) {
			r = bridge_step();
			if (r < 0) {
				gLinked = 0;
				if (gIfAttached)
					ifnet_set_flags(gIfp, 0, IFF_RUNNING);
				set_state("link lost");
				retry = now_us() + 3000000ULL;
				if (r == -2)
					gUp = 0;		/* card is dead: next connect restarts it */
			}
			continue;
		}
		if (gAuto && retry && now_us() > retry) {
			retry = 0;
			gCmd = C_AUTO;
			continue;
		}
		IOSleep(100);
	}
	gThreadRunning = 0;
	say("worker stopped");
}

static int
iwi_str_handler(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req)
{
	return sysctl_handle_string(oidp, arg1, arg2, req);
}

static int
iwi_cmd_handler(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req)
{
	int v = gCmd, error;

	error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr)
		gCmd = v;
	return error;
}

static int
iwi_text_handler(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req)
{
	char *s = (char *)arg1;

	if (arg2 == 1) {		/* status */
		snprintf(gStatusText, sizeof(gStatusText), "state=%s up=%d linked=%d mac=%02x:%02x:%02x:%02x:%02x:%02x rx=%lu tx=%lu drop=%lu outq=%d intr=%08x ridx=%u cur=%u beat=%u noin=%d mask=%d pcicmd=%08x imask=%08x crumb=%u:%s",
		    gState, gUp, gLinked, gMac[0], gMac[1], gMac[2], gMac[3], gMac[4], gMac[5], gRxPk, gTxPk, gDrops, gOutCount, gIntrSeen,
		    gRegs ? csr_read(CSR_RX_RIDX) : 0, gRxCur, gBeat, gNoInput, gInMask, gBus >= 0 ? pci_rd(gBus, gDev, gFn, 4) : 0, gRegs ? csr_read(CSR_INTR_MASK) : 0, gCrumbN, gCrumb);
		s = gStatusText;
	}
	return SYSCTL_OUT(req, s, strlen(s) + 1);
}

SYSCTL_DECL(_net);
struct sysctl_oid_list sysctl__net_iwi_children = { 0 };
struct sysctl_oid sysctl__net_iwi = { &sysctl__net_children, { 0 }, OID_AUTO, CTLTYPE_NODE | CTLFLAG_RW, (void *)&sysctl__net_iwi_children, 0, "iwi", 0, "N" };
struct sysctl_oid sysctl__net_iwi_ssid = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_STRING | CTLFLAG_RW, gSsid, sizeof(gSsid), "ssid", iwi_str_handler, "A" };
struct sysctl_oid sysctl__net_iwi_psk = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_STRING | CTLFLAG_WR, gPsk, sizeof(gPsk), "psk", iwi_str_handler, "A" };
struct sysctl_oid sysctl__net_iwi_cmd = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_INT | CTLFLAG_RW, 0, 0, "cmd", iwi_cmd_handler, "I" };
struct sysctl_oid sysctl__net_iwi_status = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_STRING | CTLFLAG_RD, gStatusText, 1, "status", iwi_text_handler, "A" };
struct sysctl_oid sysctl__net_iwi_scan = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_STRING | CTLFLAG_RD, gScanText, 0, "scan", iwi_text_handler, "A" };
struct sysctl_oid sysctl__net_iwi_dhcp = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_STRING | CTLFLAG_RD, gDhcpText, 0, "dhcp", iwi_text_handler, "A" };
struct sysctl_oid sysctl__net_iwi_log = { &sysctl__net_iwi_children, { 0 }, OID_AUTO, CTLTYPE_STRING | CTLFLAG_RD, gLog, 0, "log", iwi_text_handler, "A" };

static lck_grp_t *gGrp;

static kern_return_t
iwi_start(kmod_info_t *ki, void *data)
{
	say("loading (build " __DATE__ " " __TIME__ ")");
	gGrp = lck_grp_alloc_init("iwi", 0);
	gQLock = lck_mtx_alloc_init(gGrp, 0);
	sysctl_register_oid(&sysctl__net_iwi);
	sysctl_register_oid(&sysctl__net_iwi_ssid);
	sysctl_register_oid(&sysctl__net_iwi_psk);
	sysctl_register_oid(&sysctl__net_iwi_cmd);
	sysctl_register_oid(&sysctl__net_iwi_status);
	sysctl_register_oid(&sysctl__net_iwi_scan);
	sysctl_register_oid(&sysctl__net_iwi_log);
	sysctl_register_oid(&sysctl__net_iwi_dhcp);
	IOCreateThread(worker, 0);
	return KERN_SUCCESS;
}

static kern_return_t
iwi_stop(kmod_info_t *ki, void *data)
{
	int i;

	/* an attached interface cannot be torn down safely from here: refuse to unload once it exists */
	if (gIfAttached)
		return KERN_FAILURE;
	gQuit = 1;
	for (i = 0; i < 40 && gThreadRunning; i++)
		IOSleep(100);
	if (gThreadRunning)
		return KERN_FAILURE;
	if (gUp) {
		fw_cmd(CMD_DISABLE, 0, 0);
		csr_write(CSR_INTR_MASK, 0);
		stop_master();
		gUp = 0;
	}
	sysctl_unregister_oid(&sysctl__net_iwi_dhcp);
	sysctl_unregister_oid(&sysctl__net_iwi_log);
	sysctl_unregister_oid(&sysctl__net_iwi_scan);
	sysctl_unregister_oid(&sysctl__net_iwi_status);
	sysctl_unregister_oid(&sysctl__net_iwi_cmd);
	sysctl_unregister_oid(&sysctl__net_iwi_psk);
	sysctl_unregister_oid(&sysctl__net_iwi_ssid);
	sysctl_unregister_oid(&sysctl__net_iwi);
	return KERN_SUCCESS;
}

KMOD_EXPLICIT_DECL(com.local.iwi, "0.1.0", iwi_start, iwi_stop)

#include "wpa.c"
