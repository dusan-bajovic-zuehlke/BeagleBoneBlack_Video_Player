# Create directory with files for Debian and script to load these files to SD card

This directory contains a script that automatically downloads the official Debian image for BeagleBone Black, flashes it to an SD card and strips it on BeagleBone Black.

---

## Contents

```
bbb-debian/
├── flash.sh     ← flashing script
├── strip.sh     ← Debian striping script
└── README.md    ← this file
```


## Usage

### 1. Make the flashing script executable
```bash
chmod +x flash.sh
```

### 2. Run the flashing script as root
```bash
sudo ./flash.sh
```

### 3. Follow the prompts

The flashing script will:
- Download the Debian 12.13 (Bookworm) image 
- Verify the SHA256 checksum
- Show all connected storage devices
- Ask which device is your SD card
- Ask for confirmation before erasing anything
- Flash the image to the SD card
- Ask for username, password, hostname and timezone
- Configure `sysconf.txt` automatically

---

## Image Details

| Property | Value |
|---|---|
| OS | Debian 12.13 (Bookworm) |
| Kernel | 5.10.168-ti-r83 (TI optimized) |
| Architecture | ARMv7l (32-bit ARM) |
| Image size | ~400MB compressed / ~4GB uncompressed |
| Source | files.beagle.cc (official BeagleBoard) |

---

## Booting the BBB from SD Card

1. Insert SD card into the **underside slot** of the BBB
2. **Hold the S2 button** (next to the SD slot)
3. Apply power while holding S2
4. Hold for ~5 seconds then release
5. The 4 blue LEDs will sweep back and forth — boot is in progress
6. Wait ~60 seconds

---

### 4. Copy striping script to Debian on BeagleBone Black

```bash
scp strip.sh debian@192.168.7.2:~
```


### 5. Connecting via SSH

```bash
ssh debian@192.168.7.2
```

### 6. Make the striping script executable
```bash
chmod +x strip.sh
```

### 7. Run the striping script as root
```bash
sudo ./strip.sh
```


