#!/usr/bin/env bash
# tools/create_esp_root_image.sh -- a disk laid out the way an installer lays
# one out: an EFI System Partition first, the system volume second.
#
# This is the layout that broke the root search. Both partitions are FAT, the
# ESP comes first, and it mounts perfectly -- so a search that stopped at the
# first volume a filesystem claimed took the ESP and then found no
# /system/bin to spawn from.
#
#   p1  FAT32, at LBA 2048, holding only /EFI/BOOT -- no system hierarchy
#   p2  a copy of the real root image
#
# The ESP's type byte is a parameter because installers disagree about it:
# 0xEF is the honest label the kernel can act on, 0x0C is the one it cannot,
# which is what makes the second case worth booting separately.
#
# usage: create_esp_root_image.sh <output.img> <root.img> [esp-type]
set -euo pipefail

out="${1:?usage: create_esp_root_image.sh <output.img> <root.img> [esp-type]}"
root="${2:?missing root image}"
esp_type="${3:-ef}"

[ -f "$root" ] || { echo "create_esp_root_image: no root image at $root" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

sector=512
p1_start=2048
p1_size=131072                                  # 64 MiB, clear of the FAT32 floor
p2_start=$((p1_start + p1_size))
root_bytes=$(wc -c < "$root")
p2_size=$(((root_bytes + sector - 1) / sector))
total=$((p2_start + p2_size + 2048))            # slack past the last partition

rm -f "$out"
dd if=/dev/zero of="$out" bs=$sector count=$total status=none

sfdisk --no-reread --no-tell-kernel "$out" >/dev/null <<SFDISK
label: dos
unit: sectors
${out}1 : start=$p1_start, size=$p1_size, type=$esp_type
${out}2 : start=$p2_start, size=$p2_size, type=0c
SFDISK

# -s 1 for the same reason create_disk.sh uses it: one sector per cluster
# keeps the cluster count above the FAT32 floor at this size.
mkfs.fat -F 32 -s 1 -C "$work/esp.img" $((p1_size / 2)) >/dev/null
mmd -i "$work/esp.img" ::EFI ::EFI/BOOT
printf 'not a real bootloader' > "$work/bootx64.efi"
mcopy -i "$work/esp.img" "$work/bootx64.efi" ::EFI/BOOT/BOOTX64.EFI

dd if="$work/esp.img" of="$out" bs=$sector seek=$p1_start conv=notrunc status=none
dd if="$root" of="$out" bs=$sector seek=$p2_start conv=notrunc status=none

echo "$out created (${total} sectors, ESP type $esp_type at $p1_start, root at $p2_start)"
