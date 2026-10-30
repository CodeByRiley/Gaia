#!/usr/bin/env python3
"""Incremental Gaia ext2 root filesystem builder (requires e2fsprogs).

Run from anywhere: python3 scripts/update_disk.py [--rebuild] [--prune]
Do not update the image while Gaia/QEMU has it open.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent

# These files use deliberately different target names/casing or live outside rootfs/.
REQUIRED = {
    'rootfs/readme.txt': 'readme.txt',
    'rootfs/system/fonts/SansDisplayVariable.ttf': 'system/fonts/sansdisplayvariable.ttf',
    'rootfs/system/fonts/SansDisplayStatic.ttf': 'system/fonts/sansdisplaystatic.ttf',
    'rootfs/system/icons/DOOM.bmp': 'system/icons/doom.bmp',
    'rootfs/system/icons/disconnected.bmp': 'system/icons/disconnected.bmp',
    'rootfs/system/icons/start_icon.bmp': 'system/icons/start_icon.bmp',
    'rootfs/system/icons/connected.bmp': 'system/icons/connected.bmp',
    'rootfs/system/icons/cursor.bmp': 'system/icons/cursor.bmp',
    'rootfs/system/icons/skald.bmp': 'system/icons/skald.bmp',
    'rootfs/system/wallpaper.bmp': 'system/wallpaper.bmp',
    'rootfs/system/testcpp.elf': 'system/testcpp.elf',
}
OPTIONAL = {
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/credits.html': 'res/netsurf/credits.html',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/internal.css': 'res/netsurf/internal.css',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/licence.html': 'res/netsurf/licence.html',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/welcome.html': 'res/netsurf/welcome.html',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/netsurf.png': 'res/netsurf/netsurf.png',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/adblock.css': 'res/netsurf/adblock.css',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/default.css': 'res/netsurf/default.css',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/favicon.png': 'res/netsurf/favicon.png',
    'userspace/bin/netsurf/netsurf/resources/icons/directory.png': 'res/netsurf/icons/directory.png',
    'userspace/bin/netsurf/netsurf/frontends/framebuffer/res/quirks.css': 'res/netsurf/quirks.css',
    'userspace/bin/netsurf/netsurf/resources/icons/content.png': 'res/netsurf/icons/content.png',
    'userspace/libc/build-musl-gaia/muslhello.elf': 'usr/bin/muslhello.elf',
    'userspace/libc/build-musl-gaia/muslposix.elf': 'usr/bin/muslposix.elf',
    'userspace/libc/build-musl-gaia/muslsyscall.elf': 'usr/bin/muslsyscall.elf',
    'userspace/netsurf_compat/generated/Messages': 'res/netsurf/Messages',
    'build/sdk-smoke/sdksmoke.elf': 'usr/bin/sdksmoke.elf',
    'rootfs/system/sdk-test.elf': 'system/sdk-test.elf',
    'rootfs/firmware/ucodes_ga10x.bin': 'firmware/ucodes_ga10x.bin',
    'rootfs/firmware/ucodes_tu10x.bin': 'firmware/ucodes_tu10x.bin',
    'rootfs/firmware/gsp_ga10x.bin': 'firmware/gsp_ga10x.bin',
    'rootfs/firmware/gsp_tu10x.bin': 'firmware/gsp_tu10x.bin',
    'rootfs/music/beethoven.wav': 'music/beethoven.wav',
    'rootfs/games/doom/doom.wad': 'games/doom/doom.wad',
    'userspace/bin/holyd/tests/conditionals.hd': 'holyd/tests/conditionals.hd',
    'userspace/bin/holyd/tests/holyc_d_style.hd': 'holyd/tests/holyc_d_style.hd',
    'userspace/bin/holyd/tests/functions.hd': 'holyd/tests/functions.hd',
    'userspace/bin/holyd/samples/window.hd': 'holyd/samples/window.hd',
    'userspace/bin/holyd/tests/no_semis.hd': 'holyd/tests/no_semis.hd',
    'userspace/bin/holyd/tests/strings.hd': 'holyd/tests/strings.hd',
    'userspace/bin/holyd/tests/array.hd': 'holyd/tests/array.hd',
    'userspace/bin/holyd/samples/gui.hd': 'holyd/samples/gui.hd',
    'userspace/bin/holyd/samples/net.hd': 'holyd/samples/net.hd',
    'userspace/bin/holyd/tests/hello.hd': 'holyd/tests/hello.hd',
    'userspace/bin/holyd/tests/math.hd': 'holyd/tests/math.hd',
}
TARGET_DEST = {
    'shutdown': 'shutdown', 'heimdall': 'heimdall', 'diskinfo': 'diskinfo',
    'notepad': 'notepad', 'hermes': 'hermes', 'umount': 'umount',
    'lsdisk': 'lsdisk', 'reboot': 'reboot', 'pkill': 'pkill',
    'plist': 'plist', 'mount': 'mount', 'sync': 'sync', 'wipe': 'wipe',
    'mkfs': 'mkfs', 'tree': 'tree', 'cat': 'cat', 'sh': 'sh', 'ls': 'ls',
}

def target_destination(path: str) -> str:
    if path == 'userspace/bin/holyd.elf':
        return 'system/bin/holyd.elf'
    for name in TARGET_DEST:
        if path == f'userspace/bin/{name}/{name}.elf':
            return f'system/bin/{name}.elf'
    if path.startswith('build/userspace/pe/shims/') and path.endswith('.dll'):
        return 'system/win32/' + Path(path).name
    if re.fullmatch(r'userspace/bin/(?:[^/]+/)*[^/]+\.(?:elf|exe)', path):
        return 'usr/bin/' + Path(path).name
    raise ValueError(f'Invalid registered target: {path}')

def safe_dest(dst: str) -> str:
    p = Path(dst)
    if not dst or p.is_absolute() or '..' in p.parts or '\n' in dst or '\r' in dst or '"' in dst or '\\' in dst:
        raise ValueError(f'Unsafe destination path: {dst!r}')
    return p.as_posix()

def collect() -> dict[str, Path]:
    result: dict[str, Path] = {}
    def add(src: str | Path, dest: str, required: bool = False):
        path = ROOT / src
        if not path.is_file():
            if required:
                raise FileNotFoundError(f'Missing required payload: {path}')
            return
        dest = safe_dest(dest)
        if dest in result and result[dest] != path:
            raise ValueError(f'Duplicate destination {dest}: {result[dest]} and {path}')
        result[dest] = path

    for source, target in REQUIRED.items():
        add(source, target, True)

    # Discover installed native toolchain only. Do not resurrect stale user apps.
    rootfs = ROOT / 'rootfs'
    for directory in ('system/bin', 'system/libexec', 'system/lib', 'system/include'):
        folder = rootfs / directory
        if folder.is_dir():
            for path in sorted(folder.rglob('*')):
                if path.is_file():
                    add(path.relative_to(ROOT), path.relative_to(rootfs).as_posix())

    targets = ROOT / 'build/userspace/targets.list'
    if not targets.is_file():
        raise FileNotFoundError(f'Missing {targets}; build userspace first')
    for raw in targets.read_text().splitlines():
        source = raw.strip()
        if source:
            add(source, target_destination(source), True)

    for source, target in OPTIONAL.items():
        add(source, target)
    return result

def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()

def q(path: str) -> str:
    # debugfs command line supports quoted arguments; reject unsafe names upstream.
    if '"' in path or '\\' in path or '\n' in path:
        raise ValueError(f'Unsupported debugfs path: {path!r}')
    return f'"{path}"'

def is_ext2(image: Path) -> bool:
    if not image.is_file() or image.stat().st_size < 2048:
        return False
    with image.open('rb') as f:
        f.seek(1024 + 56)
        return f.read(2) == b'\x53\xef'

def run_debugfs(image: Path, commands: list[str], *, allow_exists: bool = True, allow_missing: bool = False):
    if not commands:
        return
    with tempfile.NamedTemporaryFile(mode='w', prefix='gaia-debugfs-', suffix='.txt', delete=False) as f:
        cmdpath = Path(f.name)
        f.write('\n'.join(commands) + '\n')
    try:
        proc = subprocess.run(['debugfs', '-w', '-f', str(cmdpath), str(image)],
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = proc.stdout
        # debugfs sometimes exits 0 after individual commands fail. Check diagnostics.
        errors = []
        for line in output.splitlines():
            low = line.lower()
            if allow_exists and ('file exists' in low or 'already exists' in low):
                continue
            if allow_missing and ('file not found' in low or 'not found by ext2_lookup' in low or 'no such file' in low):
                continue
            if ('error' in low or 'no space left' in low or 'file not found' in low or
                'not found by ext2_lookup' in low or 'could not' in low or 'invalid argument' in low or
                'permission denied' in low or 'filesystem is read-only' in low):
                errors.append(line)
        if proc.returncode != 0 or errors:
            raise RuntimeError('debugfs failed:\n' + '\n'.join(errors[:20]) + '\n' + output[-2000:])
    finally:
        cmdpath.unlink(missing_ok=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image', default=os.environ.get('IMG', 'build/disk.img'))
    ap.add_argument('--size-mib', type=int, default=int(os.environ.get('DISK_SIZE_MIB', '512')),
                    help='size for a NEW image (default: 512 MiB)')
    ap.add_argument('--rebuild', action='store_true', help='discard and recreate the disk image')
    ap.add_argument('--prune', action='store_true', help='delete removed, previously managed payloads')
    args = ap.parse_args()
    for tool in ('debugfs', 'mke2fs', 'truncate'):
        if not shutil.which(tool):
            ap.error(f'{tool} not found; install e2fsprogs')
    image = Path(args.image)
    if not image.is_absolute():
        image = ROOT / image
    image.parent.mkdir(parents=True, exist_ok=True)
    manifest = Path(str(image) + '.gaia-manifest.json')
    entries = collect()
    t0 = time.monotonic()
    new_image = args.rebuild or not image.exists()
    if image.exists() and not is_ext2(image) and not args.rebuild:
        ap.error(f'{image} is not a raw ext2 filesystem. Specify --rebuild for a fresh ext2 image.')
    if args.rebuild and image.exists():
        image.unlink()
    if new_image:
        if args.size_mib < 64:
            ap.error('--size-mib must be >= 64')
        total = sum(p.stat().st_size for p in entries.values())
        if total > (args.size_mib - 32) * 1024 * 1024:
            ap.error(f'Payloads require {total // 1048576} MiB; choose a larger --size-mib')
        subprocess.run(['truncate', '-s', f'{args.size_mib}M', str(image)], check=True)
        subprocess.run(['mke2fs', '-q', '-t', 'ext2', '-F', '-b', '1024', '-I', '256', str(image)], check=True)
        print(f'Created {args.size_mib} MiB ext2 disk: {image}', flush=True)
    elif not manifest.exists():
        print('Existing image has no tracking manifest: all managed payloads will be installed once.', flush=True)

    previous = {}
    if not new_image and manifest.exists():
        previous = json.loads(manifest.read_text()).get('files', {})
    now = {dst: {'sha256': digest(src), 'size': src.stat().st_size}
           for dst, src in entries.items()}
    changed = [dst for dst in entries if previous.get(dst) != now[dst]]
    removed = sorted(set(previous) - set(now)) if args.prune else []
    print(f'{len(entries)} files: {len(changed)} changed/new; {len(entries)-len(changed)} unchanged; '
          f'{len(removed)} pruned. Hashing took {time.monotonic()-t0:.1f}s.', flush=True)

    if changed or removed:
        needed_dirs = {'bin', 'mnt', 'usr', 'usr/bin', 'usr/local', 'usr/local/bin'} if new_image else set()
        for dst in changed:
            parts = Path(dst).parts[:-1]
            for i in range(1, len(parts)+1):
                needed_dirs.add('/'.join(parts[:i]))
        mkdir_cmds = [f'mkdir {q("/"+d)}' for d in sorted(needed_dirs, key=lambda d: (d.count('/'), d))]
        # Two passes: mkdir first; then modifications. mkdir existing directory is safe to ignore.
        print(f"Creating {len(mkdir_cmds)} directories...", flush=True)
        run_debugfs(image, mkdir_cmds)
        print("Directories created.", flush=True)
        # Remove old files in ONE debugfs invocation, then write replacements.
        # Missing paths on a newly created image are expected.
        rm_cmds = [f'rm {q("/"+dst)}' for dst in removed + changed]
        print(f"Removing {len(rm_cmds)} old entries...", flush=True)
        run_debugfs(image, rm_cmds, allow_missing=True)

        write_cmds = [f'write {q(str(entries[dst]))} {q("/"+dst)}' for dst in changed]
        print(f"Writing {len(write_cmds)} files into ext2...", flush=True)
        run_debugfs(image, write_cmds, allow_exists=False)

        print("Filesystem population complete.", flush=True)

    # Only record success after all updates have finished.
    if not args.prune:
        # Retain removed entries for a future --prune, but avoid confusing them with live source files.
        tracked = {**{k: v for k, v in previous.items() if k not in now}, **now}
    else:
        tracked = now
    tmp = manifest.with_suffix(manifest.suffix + '.tmp')
    tmp.write_text(json.dumps({'version': 1, 'files': tracked}, indent=2, sort_keys=True) + '\n')
    tmp.replace(manifest)
    print(f'Disk updated in {time.monotonic()-t0:.1f}s: {image}', flush=True)
    print('Do not run this updater while Gaia/QEMU has the disk open.', flush=True)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f'update_disk: {exc}', file=sys.stderr)
        sys.exit(1)
