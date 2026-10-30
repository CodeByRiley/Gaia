#!/usr/bin/env python3
"""Boot Gaia and run a program built only from the installed libgaia SDK.

The binary is built outside the tree, from tests/sdk_smoke, against the SDK
that `make -C userspace sdk-install` put in the Gaia toolchain's sysroot. It
has to exist before the disk image is made, because create_disk.sh packs it
as an optional payload:

    wsl: make -C userspace sdk-install
    wsl: tests/sdk_smoke_test.py --build-only
    mingw32-make build-x86_64
    tests/sdk_smoke_test.py
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from kernel_panic_test import Qmp, available_port, wait_for_text
from path_lookup_test import send_text

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests" / "sdk_smoke"
BINARY = ROOT / "build" / "sdk-smoke" / "sdksmoke.elf"


def build() -> int:
    """Build in a scratch directory outside the tree, then copy the binary in."""
    with tempfile.TemporaryDirectory(prefix="gaia-sdk-smoke-") as scratch:
        for name in ("main.c", "Makefile"):
            shutil.copy(SOURCE / name, scratch)
        result = subprocess.run(["make", "-C", scratch])
        if result.returncode != 0:
            return result.returncode
        BINARY.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(Path(scratch) / "sdksmoke.elf", BINARY)
    print(f"built {BINARY}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", default="dist/x86_64/kernel.iso")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--build-only", action="store_true",
                        help="build the SDK smoke binary and stop")
    args = parser.parse_args()

    if args.build_only:
        return build()

    iso = Path(args.iso).resolve()
    if not iso.exists():
        print(f"missing ISO: {iso}", file=sys.stderr)
        return 2
    if not BINARY.exists():
        print(f"missing {BINARY}; run with --build-only, then rebuild the ISO",
              file=sys.stderr)
        return 2

    log_path = Path("build/qemu-sdk-smoke.log").resolve()
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
        send_text(qmp, "sdksmoke\n")
        if not wait_for_text(log_path, "gaia sdk smoke: ok", deadline):
            print(log_path.read_text(encoding="utf-8", errors="replace"))
            print("SDK smoke binary did not report success", file=sys.stderr)
            return 1

        log = log_path.read_text(encoding="utf-8", errors="replace")
        if "PANIC" in log:
            print(log)
            print("kernel panic detected", file=sys.stderr)
            return 1

        print("SDK-built binary ran, exec'd itself, and got exit code 42 back")
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
