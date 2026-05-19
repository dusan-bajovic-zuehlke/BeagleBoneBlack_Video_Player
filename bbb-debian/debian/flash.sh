#!/bin/bash

# =============================================================================
# BeagleBone Black - Debian SD Card Flasher
# =============================================================================
# This script downloads the latest Debian image for BeagleBone Black
# and flashes it to an SD card.
#
# Usage: sudo ./flash.sh
# =============================================================================

set -e

# -----------------------------------------------------------------------------
# Configuration
# -----------------------------------------------------------------------------
IMAGE_URL="https://files.beagle.cc/file/beagleboard-public-2021/images/am335x-debian-12.13-base-v5.10-ti-armhf-2026-04-23-4gb.img.xz"
IMAGE_NAME="am335x-debian-12.13-base-v5.10-ti-armhf-2026-04-23-4gb.img.xz"
CHECKSUM="91f5aa80c5e0683d8d6c7990f4304ff4b82cc5427d71bb4d9b89ef33a9073425"
MOUNT_POINT="/mnt/bbb-boot"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# -----------------------------------------------------------------------------
# Helper functions
# -----------------------------------------------------------------------------
info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
error()   { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# -----------------------------------------------------------------------------
# Check if running as root
# -----------------------------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root. Use: sudo ./flash.sh"
fi

# -----------------------------------------------------------------------------
# Banner
# -----------------------------------------------------------------------------
echo ""
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}   BeagleBone Black - Debian SD Flasher     ${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""

# -----------------------------------------------------------------------------
# Step 1: Check required tools
# -----------------------------------------------------------------------------
info "Checking required tools..."
for tool in wget dd lsblk sha256sum xzcat; do
    if ! command -v $tool &> /dev/null; then
        error "Required tool '$tool' is not installed. Install it and try again."
    fi
done
success "All required tools found."
echo ""

# -----------------------------------------------------------------------------
# Step 2: Download image
# -----------------------------------------------------------------------------
if [[ -f "$IMAGE_NAME" ]]; then
    warn "Image file already exists: $IMAGE_NAME"
    read -p "Do you want to re-download it? (y/N): " REDOWNLOAD
    if [[ "$REDOWNLOAD" =~ ^[Yy]$ ]]; then
        rm -f "$IMAGE_NAME"
    fi
fi

if [[ ! -f "$IMAGE_NAME" ]]; then
    info "Downloading Debian image for BeagleBone Black..."
    info "URL: $IMAGE_URL"
    echo ""
    wget -q --show-progress "$IMAGE_URL" -O "$IMAGE_NAME" || error "Download failed. Check your internet connection."
    echo ""
    success "Download complete."
else
    info "Using existing image file: $IMAGE_NAME"
fi
echo ""

# -----------------------------------------------------------------------------
# Step 3: Verify checksum
# -----------------------------------------------------------------------------
info "Verifying SHA256 checksum..."
ACTUAL_CHECKSUM=$(sha256sum "$IMAGE_NAME" | awk '{print $1}')
if [[ "$ACTUAL_CHECKSUM" != "$CHECKSUM" ]]; then
    error "Checksum verification FAILED! The file may be corrupted. Delete it and run the script again."
fi
success "Checksum verified successfully."
echo ""

# -----------------------------------------------------------------------------
# Step 4: Detect SD card
# -----------------------------------------------------------------------------
info "Detecting connected storage devices..."
echo ""
lsblk -d -o NAME,SIZE,TRAN,MODEL | grep -v loop
echo ""

read -p "Enter the SD card device name (e.g. sdb, sdc): " SD_DEVICE
SD_DEVICE="/dev/$SD_DEVICE"

# Validate device exists
if [[ ! -b "$SD_DEVICE" ]]; then
    error "Device $SD_DEVICE does not exist."
fi

# Safety check - prevent flashing to nvme (internal SSD)
if [[ "$SD_DEVICE" == *"nvme"* ]]; then
    error "Device $SD_DEVICE appears to be an internal NVMe SSD. Refusing to flash."
fi

# Get device info
DEVICE_SIZE=$(lsblk -d -o SIZE "$SD_DEVICE" | tail -1 | tr -d ' ')
DEVICE_MODEL=$(lsblk -d -o MODEL "$SD_DEVICE" | tail -1 | tr -d ' ')
DEVICE_TRAN=$(lsblk -d -o TRAN "$SD_DEVICE" | tail -1 | tr -d ' ')

echo ""
echo -e "${YELLOW}Selected device:${NC}"
echo "  Device:    $SD_DEVICE"
echo "  Size:      $DEVICE_SIZE"
echo "  Model:     $DEVICE_MODEL"
echo "  Transport: $DEVICE_TRAN"
echo ""

# -----------------------------------------------------------------------------
# Step 5: Confirm before flashing
# -----------------------------------------------------------------------------
warn "ALL DATA ON $SD_DEVICE WILL BE PERMANENTLY ERASED!"
echo ""
read -p "Are you sure you want to flash to $SD_DEVICE? Type 'YES' to confirm: " CONFIRM

if [[ "$CONFIRM" != "YES" ]]; then
    echo ""
    info "Flashing cancelled."
    exit 0
fi
echo ""

# -----------------------------------------------------------------------------
# Step 6: Unmount all partitions
# -----------------------------------------------------------------------------
info "Unmounting all partitions on $SD_DEVICE..."
for partition in $(lsblk -ln -o NAME "$SD_DEVICE" | tail -n +2); do
    if mountpoint -q "/dev/$partition" 2>/dev/null; then
        umount "/dev/$partition" && info "Unmounted /dev/$partition"
    fi
done
success "All partitions unmounted."
echo ""

# -----------------------------------------------------------------------------
# Step 7: Flash the image
# -----------------------------------------------------------------------------
info "Flashing image to $SD_DEVICE..."
info "This will take 5-10 minutes. Do not remove the SD card."
echo ""

xzcat "$IMAGE_NAME" | dd of="$SD_DEVICE" bs=4M status=progress conv=fsync

echo ""
sync
success "Image flashed successfully!"
echo ""

# -----------------------------------------------------------------------------
# Step 8: Configure sysconf.txt
# -----------------------------------------------------------------------------
info "Configuring sysconf.txt..."

# Wait for kernel to recognize new partitions
sleep 3
partprobe "$SD_DEVICE" 2>/dev/null || true
sleep 2

# Find the boot partition (first partition)
BOOT_PARTITION="${SD_DEVICE}1"

if [[ ! -b "$BOOT_PARTITION" ]]; then
    warn "Could not find boot partition. You may need to configure sysconf.txt manually."
else
    mkdir -p "$MOUNT_POINT"
    mount "$BOOT_PARTITION" "$MOUNT_POINT"

    if [[ -f "$MOUNT_POINT/sysconf.txt" ]]; then
        # Ask for configuration
        echo ""
        read -p "Enter username (default: debian): " BBB_USER
        BBB_USER=${BBB_USER:-debian}

        read -sp "Enter password for $BBB_USER: " BBB_PASS
        echo ""

        read -p "Enter hostname (default: beaglebone): " BBB_HOST
        BBB_HOST=${BBB_HOST:-beaglebone}

        read -p "Enter timezone (default: Europe/Belgrade): " BBB_TZ
        BBB_TZ=${BBB_TZ:-Europe/Belgrade}

        # Write configuration
        sed -i "s|#user_name=beagle|user_name=$BBB_USER|" "$MOUNT_POINT/sysconf.txt"
        sed -i "s|#user_password=FooBar|user_password=$BBB_PASS|" "$MOUNT_POINT/sysconf.txt"
        sed -i "s|#hostname=BeagleBone|hostname=$BBB_HOST|" "$MOUNT_POINT/sysconf.txt"
        sed -i "s|#timezone=America/Chicago|timezone=$BBB_TZ|" "$MOUNT_POINT/sysconf.txt"

        umount "$MOUNT_POINT"
        success "sysconf.txt configured successfully."
    else
        warn "sysconf.txt not found. Configure manually after booting."
        umount "$MOUNT_POINT"
    fi
fi

echo ""

# -----------------------------------------------------------------------------
# Done
# -----------------------------------------------------------------------------
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}   SD card is ready!                        ${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Next steps:"
echo "  1. Eject the SD card:        sudo eject $SD_DEVICE"
echo "  2. Insert SD card into BBB (underside slot)"
echo "  3. Hold S2 button and apply power"
echo "  4. Wait ~60 seconds for boot"
echo "  5. Connect via SSH:          ssh debian@192.168.7.2"
echo ""
