#!/bin/bash

set -e

DEVICE=$1

# ─── Check device argument ────────────────────────────────────────────────────
if [[ -z "$DEVICE" ]]; then
    echo "Usage: sudo $0 /dev/sdX"
    echo ""
    echo "Available disks:"
    lsblk -d -o NAME,SIZE,TRAN,MODEL
    exit 1
fi

if [[ ! -b "$DEVICE" ]]; then
    echo "Error: $DEVICE is not a block device."
    exit 1
fi

# ─── Unmount any existing mounts ──────────────────────────────────────────────
echo -e "\e[34m[INFO]\e[0m Unmounting any existing mounts..."
umount "${DEVICE}"* 2>/dev/null || true

# ─── Write MLO and U-Boot as raw bytes ────────────────────────────────────────
echo -e "\e[34m[INFO]\e[0m Writing MLO and U-Boot raw..."
dd if=boot-files/MLO of="$DEVICE" bs=512 seek=256 conv=notrunc
dd if=boot-files/u-boot.img of="$DEVICE" bs=512 seek=768 conv=notrunc
sync

# ─── Copy boot files to SD card ───────────────────────────────────────────────
echo -e "\e[34m[INFO]\e[0m Copying boot files to FAT32 partition..."
mkfs.fat -F 32 "${DEVICE}1"
mount "${DEVICE}1" /mnt 
sudo cp -r boot-files/* /mnt/
umount /mnt
sync

# ─── Copy Alpine filesystem to SD card ────────────────────────────────────────
echo -e "\e[34m[INFO]\e[0m Copying Alpine filesystem files to ext4 partition..."
mkfs.ext4 "${DEVICE}2"
mount "${DEVICE}2" /mnt 
sudo cp -r alpine-fs/* /mnt/
umount /mnt
sync

echo -e "\e[34m[INFO]\e[0m \e[32mDone! Insert SD card into BeagleBone Black and hold S2 while powering on.\e[0m"


