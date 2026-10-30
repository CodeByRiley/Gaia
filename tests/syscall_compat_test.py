#!/usr/bin/env python3
"""Boot Gaia and run the Linux syscall compatibility checks (muslsyscall).

The guest program is userspace/libc/tests/syscall_compat.c, built with
userspace/libc/link_gaia_musl.sh and packaged at /usr/bin/muslsyscall.elf.
Every check prints one PASS/FAIL line to the serial log; this harness waits
for the summary line and reports the failures.

    python tests/syscall_compat_test.py [--disk build/disk-ext2.img]

With --disk the image is attached as an IDE disk, the way tools/run.bat boots
an ext2 root, so the same checks can run against either filesystem.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

from kernel_panic_test import Qmp, available_port, wait_for_text
from path_lookup_test import send_text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", default="dist/x86_64/kernel.iso")
    parser.add_argument("--disk", help="raw disk image to attach as IDE (ext2 root)")
    parser.add_argument("--memory", default="512M")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()

    iso = Path(args.iso).resolve()
    if not iso.exists():
        print(f"missing ISO: {iso}", file=sys.stderr)
        return 2

    log_path = Path("build/qemu-syscall-compat.log").resolve()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_bytes(b"")
    qmp_port = available_port()
    command = [
        args.qemu,
        "-cdrom", str(iso),
        "-serial", f"file:{log_path}",
        "-display", "none",
        "-vga", "virtio",
        "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        "-no-reboot",
        "-cpu", "max",  # the kernel refuses to boot without SSSE3
        "-m", args.memory,
        "-smp", "2",
    ]
    if args.disk:
        disk = Path(args.disk).resolve()
        # q35, as tools/run.bat boots it: its ide.0 bus is the AHCI
        # controller the kernel drives. The default i440fx has only PIIX
        # IDE, which the kernel does not, and the disk goes unseen.
        command += [
            "-machine", "q35",
            "-drive", f"id=disk,file={disk},if=none,format=raw",
            "-device", "ide-hd,drive=disk,bus=ide.0",
        ]

    proc = subprocess.Popen(command, stdin=subprocess.DEVNULL)
    qmp: Qmp | None = None
    try:
        deadline = time.monotonic() + args.timeout
        if not wait_for_text(log_path, "heimdall: ready", deadline):
            print(log_path.read_text(encoding="utf-8", errors="replace"))
            print("heimdall did not become ready", file=sys.stderr)
            return 1

        qmp = Qmp(qmp_port, deadline)
        send_text(qmp, "muslsyscall\n")
        if not wait_for_text(log_path, "syscall-compat: DONE", deadline):
            print(log_path.read_text(encoding="utf-8", errors="replace"))
            print("syscall compat run did not finish", file=sys.stderr)
            return 1

        log = log_path.read_text(encoding="utf-8", errors="replace")
        failures = [line for line in log.splitlines() if "syscall-compat: FAIL" in line]
        summary = re.search(r"syscall-compat: DONE pass=(\d+) fail=(\d+)", log)
        for line in failures:
            print(line)
        if "PANIC" in log:
            print("guest panic detected", file=sys.stderr)
            return 1
        if failures or not summary or summary.group(2) != "0":
            print(summary.group(0) if summary else "no summary", file=sys.stderr)
            return 1
        print(f"syscall compat passed ({summary.group(1)} checks)")
        return 0
    finally:
        if qmp is not None:
            qmp.close()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
