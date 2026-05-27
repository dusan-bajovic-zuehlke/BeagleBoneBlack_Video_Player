#!/bin/bash
# ============================================================
#  strip_debian.sh — Stripping Debian on BeagleBone Black
#  Purpose: video player + NTP clock, Ethernet connection
# ============================================================

set -e

if [ "$(id -u)" -ne 0 ]; then
  echo "Run the script as root: sudo ./strip_debian.sh"
  exit 1
fi

echo "========================================"
echo "  STRIPPING DEBIAN — BeagleBone Black"
echo "========================================"
echo ""

# ─────────────────────────────────────────────
# PHASE 1 — Disabling unnecessary services
# ─────────────────────────────────────────────
echo "[1/3] Disabling unnecessary services..."

systemctl disable --now \
  cockpit.socket \
  cockpit-motd.service \
  avahi-daemon.service \
  nginx.service \
  unattended-upgrades.service \
  ufw.service \
  e2scrub_reap.service \
  serial-getty@ttyGS0.service \
  serial-getty@ttyS0.service \
  iwd.service \
  bb-usb-gadgets.service \
  2>/dev/null || true

echo "  -> Services disabled."
echo ""

# ─────────────────────────────────────────────
# PHASE 2 — Removing unnecessary packages
# ─────────────────────────────────────────────
echo "[2/3] Removing unnecessary packages..."

# Python and dev libraries
echo "  -> Python and Python dev packages..."
apt remove --purge -y \
  python3 \
  python3-minimal \
  python3-pip \
  python3-pkg-resources \
  python3-setuptools \
  python3-distutils \
  python3-lib2to3 \
  python3-dev \
  python3.11-dev \
  python3-reportbug \
  python3-pysimplesoap \
  libpython3.11-dev \
  2>/dev/null || true

# C/C++ compilers and build tools
echo "  -> Build tools and compilers..."
apt remove --purge -y \
  build-essential \
  g++ \
  cpp \
  bison \
  flex \
  m4 \
  binutils \
  binutils-arm-linux-gnueabihf \
  patch \
  2>/dev/null || true

# Dev libraries (headers)
echo "  -> Dev libraries (headers)..."
apt remove --purge -y \
  libstdc++-12-dev \
  libc6-dev \
  linux-libc-dev \
  rpcsvc-proto \
  libgpiod-dev \
  libiio-dev \
  libexpat1-dev \
  libcrypt-dev \
  libnsl-dev \
  libtirpc-dev \
  zlib1g-dev \
  2>/dev/null || true

# WiFi, Bluetooth, firmware
echo "  -> WiFi, Bluetooth and firmware..."
apt remove --purge -y \
  iwd \
  hostapd \
  wireless-tools \
  wpasupplicant \
  rfkill \
  iw \
  wireguard-tools \
  bluetooth \
  bluez \
  pi-bluetooth \
  firmware-atheros \
  firmware-brcm80211 \
  firmware-libertas \
  firmware-realtek \
  2>/dev/null || true

# Web server
echo "  -> Nginx web server..."
apt remove --purge -y \
  nginx \
  nginx-common \
  libnginx-mod-http-fancyindex \
  2>/dev/null || true

# Cockpit (web admin panel)
echo "  -> Cockpit..."
apt remove --purge -y \
  cockpit \
  cockpit-bridge \
  cockpit-packagekit \
  cockpit-system \
  cockpit-ws \
  2>/dev/null || true

# Avahi and printing
echo "  -> Avahi and mDNS..."
apt remove --purge -y \
  avahi-daemon \
  avahi-utils \
  libnss-mdns \
  cups \
  cups-common \
  cups-core-drivers \
  2>/dev/null || true

# Git and version control
echo "  -> Git..."
apt remove --purge -y \
  git \
  git-lfs \
  git-man \
  2>/dev/null || true

# Documentation and help packages
echo "  -> Documentation and help packages..."
apt remove --purge -y \
  man-db \
  manpages \
  info \
  install-info \
  groff-base \
  reportbug \
  debian-faq \
  doc-debian \
  2>/dev/null || true

# Localizations and language packages
echo "  -> Localizations and language packages..."
apt remove --purge -y \
  locales \
  debconf-i18n \
  iso-codes \
  task-english \
  tasksel \
  tasksel-data \
  wamerican \
  2>/dev/null || true

# GPG and cryptography
echo "  -> GPG and cryptography..."
apt remove --purge -y \
  gnupg \
  gnupg-l10n \
  gnupg-utils \
  gpg \
  gpg-agent \
  gpg-wks-client \
  gpg-wks-server \
  gpgconf \
  gpgsm \
  dirmngr \
  cryptsetup \
  cryptsetup-bin \
  openssl \
  ssl-cert \
  2>/dev/null || true

# Network tools (not needed for video player)
echo "  -> Network tools..."
apt remove --purge -y \
  inetutils-telnet \
  traceroute \
  netcat-traditional \
  bind9-dnsutils \
  bind9-host \
  bind9-libs \
  nftables \
  ufw \
  2>/dev/null || true

# System/disk tools
echo "  -> System and disk tools..."
apt remove --purge -y \
  parted \
  fdisk \
  hdparm \
  btrfs-progs \
  dmidecode \
  pciutils \
  usbutils \
  usb-modeswitch \
  usb-modeswitch-data \
  lsof \
  2>/dev/null || true

# Miscellaneous unnecessary tools
echo "  -> Miscellaneous unnecessary tools..."
apt remove --purge -y \
  rsync \
  lua5.1 \
  hexedit \
  ncdu \
  dos2unix \
  bc \
  dialog \
  whiptail \
  ncal \
  tree \
  tio \
  overlayroot \
  cloud-guest-utils \
  pastebinit \
  can-utils \
  evemu-tools \
  device-tree-compiler \
  appstream \
  shared-mime-info \
  sgml-base \
  xml-core \
  software-properties-common \
  packagekit \
  packagekit-tools \
  unattended-upgrades \
  apt-listchanges \
  apt-utils \
  2>/dev/null || true

# Perl
echo "  -> Perl..."
apt remove --purge -y \
  perl \
  perl-modules-5.36 \
  2>/dev/null || true

# Security and passwords (not needed on embedded)
echo "  -> Cracklib and pwquality..."
apt remove --purge -y \
  cracklib-runtime \
  libpwquality-tools \
  2>/dev/null || true

# Cron, logrotate, zram
echo "  -> Cron, logrotate, zram..."
apt remove --purge -y \
  cron \
  cron-daemon-common \
  logrotate \
  zram-tools \
  unzip \
  2>/dev/null || true

# Cleaning leftover dependencies and caches
echo "  -> Cleaning dependencies and apt cache..."
apt autoremove --purge -y
apt autoclean -y
apt clean

echo "  -> Packages removed."
echo ""

# ─────────────────────────────────────────────
# PHASE 3 — Deleting unnecessary files
# ─────────────────────────────────────────────
echo "[3/3] Removing unnecessary files..."

# Documentation
rm -rf /usr/share/doc/*
rm -rf /usr/share/man/*
rm -rf /usr/share/info/*
rm -rf /usr/share/gtk-doc/

# Localizations (keep only en)
find /usr/share/locale -mindepth 1 -maxdepth 1 \
  ! -name 'en' ! -name 'en_US' -exec rm -rf {} + 2>/dev/null || true

# Localization files for programs
find /usr/share/i18n/locales -mindepth 1 \
  ! -name 'en_US' ! -name 'en_GB' -exec rm -rf {} + 2>/dev/null || true

# APT lists (not needed after cleanup)
rm -rf /var/lib/apt/lists/*

# Logs
journalctl --vacuum-size=10M 2>/dev/null || true
find /var/log -type f -name "*.log" -exec truncate -s 0 {} \;
find /var/log -type f -name "*.gz" -delete
find /var/log -type f -name "*.1" -delete

# Temp files
rm -rf /tmp/*
rm -rf /var/tmp/*

echo "  -> Files removed."
echo ""

# ─────────────────────────────────────────────
# RESULT
# ─────────────────────────────────────────────
echo "========================================"
echo "  DONE! System has been stripped."
echo "========================================"
echo ""
echo "Recommended restart: sudo reboot"
echo ""
echo "After reboot, check boot time with:"
echo "  systemd-analyze blame"
echo ""