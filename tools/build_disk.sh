#!/usr/bin/env bash
set -euo pipefail

cd /mnt/e/Projects/Weird/Gaia

# Explicit WSL path, independent of Zed's environment.
DISK_DIR="/home/waffler/gaia-disks"
DISK_IMG="$DISK_DIR/disk.img"

mkdir -p "$DISK_DIR"

if [[ "${1:-}" == "rebuild" ]]; then
    exec python3 Tools/update_disk.py \
        --image "$DISK_IMG" \
        --rebuild \
        --size-mib 512
fi

exec python3 Tools/update_disk.py \
    --image "$DISK_IMG"

echo "Copying disk image for Windows QEMU..."

cp "$HOME/gaia-disks/disk.img" \
   /mnt/e/Projects/Weird/Gaia/build/disk-ext2.img

echo "Disk ready for QEMU."
