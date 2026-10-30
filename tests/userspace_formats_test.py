#!/usr/bin/env python3
"""Boot the packaged image and exercise both userspace executable loaders."""
import subprocess
import time
from pathlib import Path
from kernel_panic_test import Qmp, available_port, wait_for_text
from path_lookup_test import QEMU_KEYS, send_text

QEMU_KEYS["_"] = "shift-minus"


def main():
    log = Path("build/qemu-userspace-formats.log").resolve()
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_bytes(b"")
    port = available_port()
    process = subprocess.Popen([
        "qemu-system-x86_64", "-cdrom", "dist/x86_64/kernel.iso",
        "-serial", f"file:{log}", "-display", "none", "-vga", "virtio",
        "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-no-reboot", "-m", "256M", "-smp", "2"], stdin=subprocess.DEVNULL)
    qmp = None
    try:
        deadline = time.monotonic() + 120
        assert wait_for_text(log, "heimdall: ready", deadline), "desktop did not start"
        qmp = Qmp(port, deadline)
        for command, expected in [
            ("/usr/bin/hello.elf", "hello from ring 3"),
            ("/usr/bin/hello.exe", "hello from ring 3"),
            ("/usr/bin/pe_test.exe", "pe_test: all checks passed"),
            ("/usr/bin/ls.exe /system/bin", "shutdown.elf"),
        ]:
            before = log.read_text(errors="replace").count(expected)
            send_text(qmp, command + "\n")
            while time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                assert "PANIC" not in text, "kernel panic"
                if text.count(expected) > before:
                    break
                time.sleep(0.05)
            else:
                raise AssertionError(f"{command}: missing {expected!r}")
            print(f"PASS: {command}")
        return 0
    finally:
        if qmp:
            qmp.close()
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

if __name__ == "__main__":
    raise SystemExit(main())
