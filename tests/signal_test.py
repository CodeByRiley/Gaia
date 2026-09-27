#!/usr/bin/env python3
"""Boot Gaia and run sigtest: signal delivery through musl.

sigtest checks each path from inside the guest and prints one line per
check; this script only launches it and reads the verdict off serial. The
checks cover a handler and its siginfo, register and FPU state surviving
rt_sigreturn, masking, EINTR out of a blocked read, delivery to a child
spinning in pure user code (the IRQ return path), a default-action kill
seen by wait4, and SIGCHLD.
"""

from __future__ import annotations

import argparse
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
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()

    iso = Path(args.iso).resolve()
    if not iso.exists():
        print(f"missing ISO: {iso}", file=sys.stderr)
        return 2

    log_path = Path("build/qemu-signal.log").resolve()
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
        "-m", "256M",
        "-smp", "2",
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
        send_text(qmp, "sigtest\n")
        finished = wait_for_text(log_path, "sigtest: all ok", deadline)

        log = log_path.read_text(encoding="utf-8", errors="replace")
        for line in log.splitlines():
            if line.startswith("sigtest:"):
                print("  " + line.strip())

        if "PANIC" in log:
            print(log[-4000:])
            print("kernel panic detected", file=sys.stderr)
            return 1
        if "unknown syscall" in log:
            print("kernel reported unknown syscall", file=sys.stderr)
            return 1
        if "FAILED" in log or not finished:
            print("sigtest did not pass", file=sys.stderr)
            return 1

        print("signals ok: every sigtest check passed")
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
