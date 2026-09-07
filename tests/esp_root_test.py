#!/usr/bin/env python3
"""Boot a disk laid out the way an installer lays one out, and check the root.

An EFI System Partition is FAT, comes first, and mounts perfectly. A root
search that stopped at the first volume some filesystem claimed therefore took
the ESP, and the boot ended at "process_spawn: fopen failed" -- winman lives on
the system volume, which never got mounted.

Two boots, because the kernel has two defences and only one of them applies at
a time:

  ef  the ESP is labelled 0xEF, so the partition scanner flags it and the root
      search never offers it to a filesystem at all
  0c  the ESP is labelled 0x0C, indistinguishable from a data volume, so the
      search has to mount it, find no system hierarchy, and unmount it again

The second case is the one that also proves the unmount path: both volumes are
FAT and the FAT engine holds one volume at a time, so a root that is released
incompletely leaves the real root unmountable.

Requires a built ISO and build/disk-fat.img
(tools/create_esp_root_image.sh builds the disks). Only copies under
build/tests are touched; never pass a physical host disk.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

from kernel_panic_test import wait_for_text

ESP_SECTORS = 131072
ESP_START = 2048
ROOT_START = ESP_START + ESP_SECTORS


def boot(qemu: str, iso: Path, disk: Path, log: Path, timeout: float,
         expected: list[tuple[str, str]], forbidden: list[tuple[str, str]]) -> None:
    log.write_bytes(b"")
    command = [
        qemu, "-cdrom", str(iso),
        # The hard disk outranks the CD in SeaBIOS's default order and is not
        # bootable, so without this the machine sits at "no bootable device".
        "-boot", "d",
        "-serial", f"file:{log}", "-display", "none", "-vga", "virtio",
        "-no-reboot", "-m", "512M", "-smp", "2", "-machine", "q35",
        "-drive", f"if=none,id=espdisk,file={disk},format=raw",
        "-device", "ide-hd,drive=espdisk,bus=ide.0",
    ]
    proc = subprocess.Popen(command, stdin=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + timeout
        for text, why in expected:
            if not wait_for_text(log, text, deadline):
                raise RuntimeError(
                    f"{why}: never saw {text!r}\n"
                    f"{log.read_text(errors='replace')}")
        text = log.read_text(errors="replace")
        if "PANIC" in text:
            raise RuntimeError(f"kernel panic\n{text}")
        for needle, why in forbidden:
            if needle in text:
                raise RuntimeError(f"{why}: saw {needle!r}\n{text}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", default="dist/x86_64/kernel.iso")
    parser.add_argument("--timeout", type=float, default=200)
    args = parser.parse_args()

    iso = Path(args.iso).resolve()
    if not iso.exists():
        print(f"missing ISO: {iso}", file=sys.stderr)
        return 2

    tests = Path("build/tests")
    tests.mkdir(parents=True, exist_ok=True)
    root_image = Path("build/disk-fat.img")
    if not root_image.exists():
        print(f"missing root image: {root_image}", file=sys.stderr)
        return 2

    # Both partitions are published whichever label the ESP carries, and at
    # the LBAs the image builder wrote: a scanner that found the table but
    # misread the offsets fails here rather than further down.
    common = [
        (f"partition: ahci0p1 at LBA {ESP_START}, {ESP_SECTORS} sectors",
         "ESP not published"),
        (f"partition: ahci0p2 at LBA {ROOT_START},",
         "system partition not published"),
    ]
    # The root must come from p2 and the desktop must come up: mounting the
    # right volume is only half the claim, the other half is that a process
    # can be spawned from it.
    tail = [
        ("rootfs: fat mounted from ahci0p2 at /", "root did not come from p2"),
        ("winman: ready", "desktop did not come up"),
    ]
    forbidden = [
        ("mounted from ahci0p1 at /", "the ESP was mounted as the root"),
        ("process_spawn: cannot open executable", "a spawn found no binary"),
    ]

    cases = [
        ("ef", [("rootfs: skipping EFI system partition ahci0p1",
                 "a labelled ESP was still offered to a filesystem")]),
        ("0c", [("rootfs: no system hierarchy on ahci0p1",
                 "an unlabelled ESP was accepted as a root")]),
    ]

    for esp_type, middle in cases:
        disk = (tests / f"esp-root-{esp_type}.img").resolve()
        if not disk.exists():
            print(f"missing disk image: {disk}\n"
                  f"  build it with: bash tools/create_esp_root_image.sh "
                  f"{disk} {root_image} {esp_type}", file=sys.stderr)
            return 2
        log = (tests / f"esp-root-{esp_type}.log").resolve()
        boot(args.qemu, iso, disk, log, args.timeout,
             common + middle + tail, forbidden)
        print(f"esp({esp_type}): root came from ahci0p2, desktop up; {log}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
