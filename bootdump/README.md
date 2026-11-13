# 855GM bootable register dumper
Boots from a USB stick (legacy BIOS), reads PCI config of 00:00.0 / 00:02.0 / 00:02.1 and MMIO ranges
0x2000-0x23ff, 0x6000-0x63ff, 0x61000-0x613ff, 0x70000-0x71fff, writes them to LBA 1228800 (32 sectors) and shows a summary.
Purpose: warm-restart out of Windows XP (Intel driver loaded, 3D active) into this, to see which settings a working
driver leaves; compare with a cold-boot dump (`parse_dump.py a.bin b.bin` prints differences).
Read back: `sudo dd if=/dev/rdiskN bs=512 skip=1228800 count=32 of=dump.bin; ./parse_dump.py dump.bin`.
