#!/usr/bin/env python3
"""Boot a toolchain image and drive the native GCC through its smoke ladder.

The steps go from driver-only (version, search dirs) to the full pipeline
(cc1, as, ld). Each runs from the console shell; the harness waits for the
guest to go quiet, then runs /usr/bin/muslhello as a marker and waits for
its line, so every step's serial output can be cut out and reported
separately. (Shell builtins such as echo print to the console only; a
program's write() is what also reaches the serial log.) Nothing is
asserted beyond "the marker came back"; the report is the point, since the
answer to "which step fails, and why" is what the syscall work needs.

Build the image first (the toolchain is ~280 MiB, so it is not in the
default test ISO):

    IMG=build/disk-gcc.img bash tools/create_disk.sh
    DISK_IMAGE=build/disk-gcc.img OUT_ISO=build/gcc.iso bash tools/build_iso.sh
    python tests/gcc_native_smoke_test.py --iso build/gcc.iso
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

from kernel_panic_test import Qmp, available_port, wait_for_text

KEYS = {
    " ": "spc", "/": "slash", ".": "dot", "-": "minus", ",": "comma",
    "=": "equal", ";": "semicolon", "_": "shift-minus", "(": "shift-9",
    ")": "shift-0", "{": "shift-bracket_left", "}": "shift-bracket_right",
    "|": "shift-backslash", ":": "shift-semicolon", ">": "shift-dot", "<": "shift-comma",
    '"': "shift-apostrophe", "#": "shift-3", "*": "shift-8",
}

RUN = "muslsyscall run PATH=/system/bin:/usr/bin /system/bin/gcc"

STEPS = [
    "mkdir /gcct",
    "cd /gcct",
    "write hello.c int main(void){return 0;}",
    "gcc --version",
    "gcc -v",
    "gcc -print-search-dirs",
    "gcc -print-prog-name=cc1",
    "gcc -S hello.c -o hello.s",
    # The shell starts programs with no environment, so GCC above cannot
    # locate itself. The same ladder again through muslsyscall's env-style
    # runner, which execs with a PATH and an absolute argv[0].
    f"{RUN} --version",
    f"{RUN} -print-prog-name=cc1",
    f"{RUN} -S hello.c -o hello.s",
    f"{RUN} -c hello.c -o hello.o",
    "cat hello.s",
    "muslsyscall run /system/bin/readelf -hS hello.o",
    f"{RUN} hello.o -o hello.elf",
    "run /gcct/hello.elf",
]


MARKER = "hello from musl on Gaia"
NOISE = ("[KERNEL]: exit prev", "[KERNEL]: exit next", "[KERNEL]: [e1000]", "[KERNEL]: eth_input",
         "[KERNEL]: ARP", "[KERNEL]: netmon", "[KERNEL]: elf: segment")


def wait_for_count(log: Path, text: str, count: int, deadline: float) -> bool:
    while time.monotonic() < deadline:
        if log.read_bytes().decode("utf-8", "replace").count(text) >= count:
            return True
        time.sleep(0.5)
    return False


def send_text(qmp: Qmp, value: str) -> None:
    for char in value:
        if char == "\n":
            key = "ret"
        elif char.isupper():
            key = "shift-" + char.lower()
        else:
            key = KEYS.get(char, char)
        qmp.command("human-monitor-command", {"command-line": f"sendkey {key}"})
        time.sleep(0.08)


def wait_quiet(log: Path, quiet: float, deadline: float) -> None:
    size, since = -1, time.monotonic()
    while time.monotonic() < deadline:
        now = log.stat().st_size
        if now != size:
            size, since = now, time.monotonic()
        elif time.monotonic() - since >= quiet:
            return
        time.sleep(0.5)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", default="build/gcc.iso")
    parser.add_argument("--memory", default="1G")
    parser.add_argument("--disk", help="raw disk image to attach as the AHCI root (ext2)")
    parser.add_argument("--timeout", type=float, default=900)
    parser.add_argument("--step-timeout", type=float, default=300)
    parser.add_argument("--only", action="append", default=[],
                        help="run only steps containing this text (repeatable)")
    parser.add_argument("--step", action="append", default=[],
                        help="run these shell commands instead of the ladder (repeatable)")
    args = parser.parse_args()
    steps = args.step or [s for s in STEPS if not args.only or any(o in s for o in args.only)]

    iso = Path(args.iso).resolve()
    if not iso.exists():
        print(f"missing ISO: {iso}", file=sys.stderr)
        return 2
    log_path = Path("build/qemu-gcc-smoke.log").resolve()
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_bytes(b"")
    qmp_port = available_port()
    command = [
        args.qemu, "-cdrom", str(iso), "-serial", f"file:{log_path}",
        "-display", "none", "-vga", "virtio",
        "-qmp", f"tcp:127.0.0.1:{qmp_port},server=on,wait=off",
        "-no-reboot", "-cpu", "max", "-m", args.memory, "-smp", "2",
    ]
    if args.disk:
        # q35's ide.0 is the AHCI controller the kernel drives; see
        # tests/syscall_compat_test.py.
        command += [
            "-machine", "q35",
            "-drive", f"id=disk,file={Path(args.disk).resolve()},if=none,format=raw",
            "-device", "ide-hd,drive=disk,bus=ide.0",
        ]
    proc = subprocess.Popen(command, stdin=subprocess.DEVNULL)
    qmp: Qmp | None = None
    reached = 0
    try:
        deadline = time.monotonic() + args.timeout
        if not wait_for_text(log_path, "heimdall: ready", deadline):
            print("heimdall did not become ready", file=sys.stderr)
            return 1
        qmp = Qmp(qmp_port, deadline)
        for index, step in enumerate(steps):
            start = log_path.stat().st_size
            send_text(qmp, step + "\n")
            step_deadline = min(deadline, time.monotonic() + args.step_timeout)
            wait_quiet(log_path, 4.0, step_deadline)
            send_text(qmp, "muslhello\n")
            ok = wait_for_count(log_path, MARKER, index + 1, step_deadline + 60)
            text = log_path.read_bytes()[start:].decode("utf-8", "replace")
            print(f"===== {step} {'(completed)' if ok else '(NO MARKER: hung?)'}")
            for line in text.splitlines():
                if MARKER in line or line.startswith(NOISE):
                    continue
                print("   ", line)
            if not ok:
                break
            reached = index + 1
        print(f"gcc smoke: {reached}/{len(steps)} steps returned to the shell")
        return 0 if reached == len(steps) else 1
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
