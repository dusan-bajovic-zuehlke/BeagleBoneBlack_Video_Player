# Unpacking Boot Files to SD Card (BeagleBone Black)

---

## Prerequisites

SD card formatted and mounted (see [SD card formatting guide](https://github.com/DusanBajovicZuehlke/BeagleBoneBlack_Video_Player/blob/formating-sd-card/docs/formating-sd-card.md))
- `beagle-source/` folder containing:
  - [boot-files.tar.gz](https://github.com/DusanBajovicZuehlke/BeagleBoneBlack_Video_Player/blob/beagle_alpine/beagle-sources/alpine-fs.tar.gz)
  - [alpine-fs.tar.gz](https://github.com/DusanBajovicZuehlke/BeagleBoneBlack_Video_Player/blob/beagle_alpine/beagle-sources/boot-files.tar.gz)
- Partitions mounted:
  - `/dev/sdb1` → `/mnt/bbb-boot`
  - `/dev/sdb2` → `/mnt/bbb-root`

  ⚠️ Note: Your SD card may not always appear as /dev/sdb — it could be /dev/sdc, /dev/sdd, etc., depending on how many storage devices are connected. Always run lsblk first to confirm the correct device name before running any commands. Replace sdb with the correct device throughout this guide.

---

## Optional: Automated Installation Script

If you want to automate the extraction process or continue with additional setup steps after verifying that the required files exist, you can use the provided [Alpine installation script](https://github.com/DusanBajovicZuehlke/BeagleBoneBlack_Video_Player/blob/formating-sd-card/beagle-sources/write_alpine.sh) instead of running the commands manually.

The script performs the same extraction and verification steps automatically and can be used as a faster alternative to the manual process described below.

## 1. Verify the Source Files Exist

```bash
ls ~/beagle-source/
```

Expected output:

```
alpine-fs.tar.gz  boot-files.tar.gz
```

---

## 2. Unpack Boot Files to sdb1 (bbb-boot)

Extract `boot-files.tar.gz` into the boot partition:

```bash
sudo tar -xzvf ~/beagle-source/boot-files.tar.gz -C /mnt/bbb-boot
```

Verify the contents:

```bash
ls /mnt/bbb-boot
```

---

## 3. Unpack Alpine Filesystem to sdb2 (bbb-root)

Extract `alpine-fs.tar.gz` into the root partition:

```bash
sudo tar -xzvf ~/beagle-source/alpine-fs.tar.gz -C /mnt/bbb-root
```

Verify the contents:

```bash
ls /mnt/bbb-root
```

---

## 4. Sync and Unmount

After extraction is complete, flush all writes and unmount the card safely:

```bash
sync
sudo umount /dev/sdb*
```

> ⚠️ Always run `sync` before unplugging — skipping it can result in corrupted or incomplete files on the SD card.

---

## Summary

| Archive | Source | Destination |
|---------|--------|-------------|
| `boot-files.tar.gz` | `~/beagle-source/` | `/mnt/bbb-boot` (sdb1) |
| `alpine-fs.tar.gz` | `~/beagle-source/` | `/mnt/bbb-root` (sdb2) |
