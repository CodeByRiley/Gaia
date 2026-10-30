#!/usr/bin/env python3
"""Run stock-toolchain Linux/Windows binaries, DLLs, threads, and GUI input."""
import subprocess
import shutil
import os
import time
from pathlib import Path
from kernel_panic_test import Qmp, available_port, read_ppm, wait_for_text
from path_lookup_test import QEMU_KEYS, send_text

QEMU_KEYS["_"] = "shift-minus"


def read_log(path):
    for _ in range(20):
        try:
            return path.read_text(errors="replace")
        except OSError:
            time.sleep(0.02)
    raise RuntimeError("serial log remained unavailable")


def main():
    log = Path(f"build/qemu-compatibility-{os.getpid()}.log").resolve()
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_bytes(b"")
    # Keep the boot medium stable while another development build rewrites the ISO.
    source = Path("dist/x86_64/kernel.iso")
    snapshot = Path(f"build/qemu-compatibility-{os.getpid()}.iso")
    for _ in range(5):
        before = source.stat()
        shutil.copyfile(source, snapshot)
        after = source.stat()
        if (before.st_mtime_ns, before.st_size) == (after.st_mtime_ns, after.st_size):
            break
    else:
        raise RuntimeError("ISO kept changing during snapshot")
    port = available_port()
    process = subprocess.Popen([
        "qemu-system-x86_64", "-cdrom", str(snapshot),
        "-serial", f"file:{log}", "-display", "none", "-vga", "virtio",
        "-qmp", f"tcp:127.0.0.1:{port},server=on,wait=off",
        "-no-reboot", "-m", "256M", "-smp", "2"], stdin=subprocess.DEVNULL)
    qmp = None
    try:
        deadline = time.monotonic() + 120
        assert wait_for_text(log, "heimdall: ready", deadline), "desktop did not start"
        qmp = Qmp(port, deadline)
        for command, expected in [
            ("/usr/bin/winhello.exe", "winhello: all checks passed"),
            ("/usr/bin/linuxhello.elf", "linuxhello: all checks passed"),
        ]:
            before = read_log(log).count(expected)
            send_text(qmp, command + "\n")
            while time.monotonic() < deadline:
                text = read_log(log)
                assert "PANIC" not in text and "USER SEGFAULT" not in text and "stack smashing" not in text, text[-5000:]
                if text.count(expected) > before:
                    break
                time.sleep(0.05)
            else:
                raise AssertionError(f"{command}: missing {expected!r}")
            print(f"PASS: {command}")
        assert "winhello: DLL lifecycle checks passed" in read_log(log), "test DLLs were not packaged"
        send_text(qmp, "/usr/bin/wingui.exe\n")
        assert wait_for_text(log, "wingui: ready", deadline), read_log(log)[-5000:]
        time.sleep(0.3)
        screenshot = Path("build/win32-gui.ppm").resolve()
        qmp.command("screendump", {"filename": str(screenshot)})
        width, height, pixels = read_ppm(screenshot)
        color = bytes((35, 80, 145))
        colored = [i // 3 for i in range(0, len(pixels), 3) if pixels[i:i+3] == color]
        assert len(colored) > 20000, "GDI FillRect did not reach the Heimdall framebuffer"
        # Move from the initial pointer origin into the client region and click.
        middle = colored[len(colored)//2]
        x, y = middle % width, middle // width
        qmp.command("human-monitor-command", {"command-line": f"mouse_move {x} {y}"})
        qmp.command("human-monitor-command", {"command-line": "mouse_button 1"})
        time.sleep(0.1)
        qmp.command("human-monitor-command", {"command-line": "mouse_button 0"})
        assert wait_for_text(log, "wingui: mouse input", deadline), "mouse dispatch failed"
        send_text(qmp, "a")
        assert wait_for_text(log, "wingui: keyboard input", deadline), "keyboard dispatch failed"
        send_text(qmp, "q")
        assert wait_for_text(log, "wingui: destroyed", deadline), "window lifetime failed"
        print("PASS: Win32 GUI drawing, mouse, keyboard, message loop and close")
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
        snapshot.unlink(missing_ok=True)

if __name__ == "__main__":
    raise SystemExit(main())
