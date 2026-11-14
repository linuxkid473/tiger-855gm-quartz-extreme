# Disk merge / in-place HFS+ grow (tools)

The laptop disk was Tiger + Windows XP + ZETA (BeOS-derived). XP was removed and Tiger's HFS+ volume grown into its space, ZETA moved to the end of the disk.

* `tools/hfsgrow.py DEVICE NEW_TOTAL_BLOCKS EXT_START [--yes]` - grows an **unmounted, cleanly journaled** HFS+ volume in place: extends the allocation bitmap through extra extents placed in the new free area, frees the old alternate-header block, marks the new last block, rewrites primary + alternate volume headers (alternate at volume end - 1024). The partition must end exactly at `total*4096` bytes. Refuses non-clean journals. Dry-run by default.
* `tools/hfsverify.py` - read-only consistency check of bitmap vs header counts.
* ZETA's boot sector stores its partition start LBA at offset 171 (`66 05 imm32`); the BVBP loader config (sector 61) references partitions by number, so after reordering the partition table the config's `PART=` must be updated (it was left at 12 and had to become 9).

Clone/backup plan: from a GParted live USB stream `/dev/sda` through `zstd`, hash it, restore with `zstd -dc image | dd of=/dev/sdX`.
