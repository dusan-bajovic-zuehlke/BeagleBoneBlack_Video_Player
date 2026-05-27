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

echo "Device: $DEVICE"
lsblk "$DEVICE"
read -rp "Are you sure you want to write to $DEVICE? (yes/no): " CONFIRM
if [[ "$CONFIRM" != "yes" ]]; then
    echo "Aborted."
    exit 0
fi

# ─── Unmount any existing mounts ──────────────────────────────────────────────
echo ">> Unmounting any existing mounts..."
umount "${DEVICE}"* 2>/dev/null || true

# ─── Extract boot files to temp dir ───────────────────────────────────────────
echo ">> Extracting boot files..."
WORKDIR=$(mktemp -d)
tar xf boot-files.tar.gz --strip-components=1 -C "$WORKDIR"

# ─── Write MLO and U-Boot raw ─────────────────────────────────────────────────
echo ">> Writing MLO and U-Boot raw..."
dd if="$WORKDIR/MLO" of="$DEVICE" bs=512 seek=256 conv=notrunc
dd if="$WORKDIR/u-boot.img" of="$DEVICE" bs=512 seek=768 conv=notrunc
sync

# ─── Mount FAT32 and copy boot files ──────────────────────────────────────────
echo ">> Mounting FAT32 partition and copying boot files..."
mount "${DEVICE}1" /mnt
cp "$WORKDIR"/* /mnt/
umount /mnt
sync

rm -rf "$WORKDIR"

# ─── Mount ext4 and copy Alpine filesystem ────────────────────────────────────
echo ">> Mounting ext4 partition and copying Alpine filesystem..."
WORKDIR=$(mktemp -d)
tar xf alpine-fs.tar.gz --strip-components=1 -C "$WORKDIR"
mount "${DEVICE}2" /mnt
cp -r "$WORKDIR"/* /mnt/
umount /mnt
sync

rm -rf "$WORKDIR"

echo ""
echo "✓ Done! Insert SD card into BeagleBone Black and hold S2 while powering on."
