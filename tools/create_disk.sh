#!/usr/bin/env bash
set -euo pipefail

export PATH="$(getent passwd "$(id -u)" | cut -d: -f6)/opt/gaia/bin:$PATH"

# Run from the repository root; every path below is repo-relative.
cd "$(dirname "$0")/.."

# ===========================================================================
# Configuration (override via environment)
# ===========================================================================
ROOTFS_TYPE="${GAIA_ROOTFS_TYPE:-fat}"    # fat | ext2
IMG="${IMG:-build/disk.img}"
MIN_SIZE=64
SLACK_MIB="${SLACK_MIB:-32}"              # reserved headroom on top of payload (was 32)
STRIP_BINS="${STRIP_BINS:-1}"             # strip ELF executables — biggest size win
STRIP_LIBS="${STRIP_LIBS:-1}"             # strip debug info from .a / .o (keeps symbols)
VERBOSE="${VERBOSE:-0}"                   # set to 1 for a per-directory size breakdown
# ===========================================================================
# Tool checks
# ===========================================================================
case "$ROOTFS_TYPE" in
    fat)  required_tools=(mkfs.fat mmd mdir mcopy) ;;
    ext2) required_tools=(mke2fs e2fsck debugfs) ;;
    *)
        echo "create_disk: unsupported filesystem '$ROOTFS_TYPE'" >&2
        exit 1
        ;;
esac
for tool in "${required_tools[@]}"; do
    command -v "$tool" >/dev/null || {
        echo "create_disk: $tool not found on PATH" >&2
        echo "  Debian/Ubuntu: sudo apt install dosfstools mtools e2fsprogs" >&2
        echo "  Fedora:        sudo dnf install dosfstools mtools e2fsprogs" >&2
        echo "  Arch:          sudo pacman -S dosfstools mtools e2fsprogs" >&2
        echo "  On Windows this step is meant to run inside WSL; see README." >&2
        exit 1
    }
done

# ===========================================================================
# Build the payload list — each entry is "source::destination"
# ===========================================================================
payloads=(
    "rootfs/readme.txt::readme.txt"

    "rootfs/system/fonts/SansDisplayVariable.ttf::system/fonts/sansdisplayvariable.ttf"
    "rootfs/system/fonts/SansDisplayStatic.ttf::system/fonts/sansdisplaystatic.ttf"
    "rootfs/system/icons/disconnected.bmp::system/icons/disconnected.bmp"
    "rootfs/system/icons/start_icon.bmp::system/icons/start_icon.bmp"
    "rootfs/system/icons/connected.bmp::system/icons/connected.bmp"
    "rootfs/system/icons/cursor.bmp::system/icons/cursor.bmp"
    "rootfs/system/icons/skald.bmp::system/icons/skald.bmp"
    "rootfs/system/icons/DOOM.bmp::system/icons/doom.bmp"
    "rootfs/system/wallpaper.bmp::system/wallpaper.bmp"
    "rootfs/system/testcpp.elf::system/testcpp.elf"
    "rootfs/test.cpp::test.cpp"
)

# Dynamically package the native development toolchain.
#   rootfs/system/libexec/gcc/.../cc1  →  /system/libexec/gcc/.../cc1
# Preserves the path relative to rootfs/.
toolchain_dirs=(
    "rootfs/system/bin"
    "rootfs/system/libexec"
    "rootfs/system/lib"
    "rootfs/system/include"
)
# GAIA_TOOLCHAIN=0 leaves the toolchain out (~280 MiB): syscall and smoke
# tests that never run gcc boot a far smaller image that way.
[[ "${GAIA_TOOLCHAIN:-1}" == 0 ]] && toolchain_dirs=()
for dir in "${toolchain_dirs[@]}"; do
    [[ -d "$dir" ]] || continue
    while IFS= read -r -d '' file; do
        payloads+=("$file::${file#rootfs/}")
    done < <(find -L "$dir" -type f -print0 | sort -z)
done

# Optional payloads: anything gitignored, unbuilt, or hardware-specific.
# A clean checkout must still produce a bootable image without them.
optional_payloads=(
    # *.wav is gitignored, so a fresh clone has no way to produce this.
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/credits.html::res/netsurf/credits.html"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/internal.css::res/netsurf/internal.css"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/licence.html::res/netsurf/licence.html"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/welcome.html::res/netsurf/welcome.html"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/netsurf.png::res/netsurf/netsurf.png"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/adblock.css::res/netsurf/adblock.css"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/default.css::res/netsurf/default.css"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/favicon.png::res/netsurf/favicon.png"
    "userspace/bin/netsurf/netsurf/resources/icons/directory.png::res/netsurf/icons/directory.png"
    "userspace/bin/netsurf/netsurf/frontends/framebuffer/res/quirks.css::res/netsurf/quirks.css"
    "userspace/bin/netsurf/netsurf/resources/icons/content.png::res/netsurf/icons/content.png"
    "userspace/libc/build-musl-gaia/muslhello.elf::usr/bin/muslhello.elf"
    "userspace/libc/build-musl-gaia/muslposix.elf::usr/bin/muslposix.elf"
    "userspace/libc/build-musl-gaia/muslsyscall.elf::usr/bin/muslsyscall.elf"
    "userspace/netsurf_compat/generated/Messages::res/netsurf/Messages"
    "rootfs/firmware/ucodes_ga10x.bin::firmware/ucodes_ga10x.bin"
    "rootfs/firmware/ucodes_tu10x.bin::firmware/ucodes_tu10x.bin"
    "rootfs/firmware/gsp_ga10x.bin::firmware/gsp_ga10x.bin"
    "rootfs/firmware/gsp_tu10x.bin::firmware/gsp_tu10x.bin"

    "rootfs/system/sdk-test.elf::system/sdk-test.elf"

    # Built from the installed SDK outside this tree; see tests/sdk_smoke_test.py.
    "build/sdk-smoke/sdksmoke.elf::usr/bin/sdksmoke.elf"

    "rootfs/music/beethoven.wav::music/beethoven.wav"
    "rootfs/games/doom/doom.wad::games/doom/doom.wad"

    # The .hd scripts come from the HolyD submodule, which is their only home now.
    # The tests-vs-samples split is load-bearing: `holyd --test` runs every .hd
    # under holyd/tests and waits for each to finish, so a script that waits on a
    # person or the network would hang the run. Those are samples, run by path.
    "userspace/bin/holyd/tests/conditionals.hd::holyd/tests/conditionals.hd"
    "userspace/bin/holyd/tests/holyc_d_style.hd::holyd/tests/holyc_d_style.hd"
    "userspace/bin/holyd/tests/functions.hd::holyd/tests/functions.hd"
    "userspace/bin/holyd/samples/window.hd::holyd/samples/window.hd"
    "userspace/bin/holyd/tests/no_semis.hd::holyd/tests/no_semis.hd"
    "userspace/bin/holyd/tests/strings.hd::holyd/tests/strings.hd"
    "userspace/bin/holyd/tests/array.hd::holyd/tests/array.hd"
    "userspace/bin/holyd/samples/gui.hd::holyd/samples/gui.hd"
    "userspace/bin/holyd/samples/net.hd::holyd/samples/net.hd"
    "userspace/bin/holyd/tests/hello.hd::holyd/tests/hello.hd"
    "userspace/bin/holyd/tests/math.hd::holyd/tests/math.hd"
)

# Only the active build graph may supply executables. A removed manifest or
# disabled port must not reappear merely because an old binary is on disk.
target_list="build/userspace/targets.list"
if [[ ! -f "$target_list" ]]; then
    echo "create_disk: missing $target_list; build userspace first" >&2
    exit 1
fi

# Resolve a registered target's destination path.
map_target() {
    case "$1" in
        userspace/bin/shutdown/shutdown.elf) echo "system/bin/shutdown.elf" ;;
        userspace/bin/heimdall/heimdall.elf) echo "system/bin/heimdall.elf" ;;
        userspace/bin/diskinfo/diskinfo.elf) echo "system/bin/diskinfo.elf" ;;
        userspace/bin/notepad/notepad.elf)   echo "system/bin/notepad.elf" ;;
        userspace/bin/hermes/hermes.elf)     echo "system/bin/hermes.elf" ;;
        userspace/bin/umount/umount.elf)     echo "system/bin/umount.elf" ;;
        userspace/bin/lsdisk/lsdisk.elf)     echo "system/bin/lsdisk.elf" ;;
        userspace/bin/reboot/reboot.elf)     echo "system/bin/reboot.elf" ;;
        userspace/bin/pkill/pkill.elf)       echo "system/bin/pkill.elf" ;;
        userspace/bin/plist/plist.elf)       echo "system/bin/plist.elf" ;;
        userspace/bin/mount/mount.elf)       echo "system/bin/mount.elf" ;;
        userspace/bin/sync/sync.elf)         echo "system/bin/sync.elf" ;;
        userspace/bin/wipe/wipe.elf)         echo "system/bin/wipe.elf" ;;
        userspace/bin/mkfs/mkfs.elf)         echo "system/bin/mkfs.elf" ;;
        userspace/bin/tree/tree.elf)         echo "system/bin/tree.elf" ;;
        userspace/bin/holyd.elf)             echo "system/bin/holyd.elf" ;;
        userspace/bin/cat/cat.elf)           echo "system/bin/cat.elf" ;;
        userspace/bin/chmod/chmod.elf)       echo "system/bin/chmod.elf" ;;
        userspace/bin/sh/sh.elf)             echo "system/bin/sh.elf" ;;
        userspace/bin/ls/ls.elf)             echo "system/bin/ls.elf" ;;
        build/userspace/pe/shims/*.dll)      echo "system/win32/${1##*/}" ;;
        userspace/bin/*.elf|userspace/bin/*.exe) echo "usr/bin/${1##*/}" ;;
        *) return 1 ;;
    esac
}

while IFS= read -r host_path; do
    host_path="${host_path%$'\r'}"
    [[ -z "$host_path" ]] && continue
    destination="$(map_target "$host_path")" || {
        echo "create_disk: invalid registered target: $host_path" >&2
        exit 1
    }
    [[ -f "$host_path" ]] || { echo "missing registered binary: $host_path" >&2; exit 1; }
    payloads+=("$host_path::$destination")
done < "$target_list"

# Fold in optional payloads that exist on disk.
for entry in "${optional_payloads[@]}"; do
    host_path="${entry%%::*}"
    [[ -f "$host_path" ]] && payloads+=("$entry")
done

# ===========================================================================
# Stage everything into a temporary tree.
# We strip the *copies* so the host toolchain stays intact, and both the FAT
# and ext2 backends read from the same stripped tree.
# ===========================================================================
staging="$(mktemp -d)"
trap 'rm -rf -- "$staging"' EXIT

for entry in "${payloads[@]}"; do
    source="${entry%%::*}"
    dest="${entry##*::}"
    [[ -f "$source" ]] || { echo "missing rootfs payload: $source" >&2; exit 1; }
    mkdir -p "$staging/$(dirname "$dest")"
    cp "$source" "$staging/$dest"
done

# Keep the standard executable hierarchy present even when a directory has
# no packaged files yet. /usr/local/bin is reserved for post-install tools.
for d in bin mnt usr/bin usr/local/bin; do
    mkdir -p "$staging/$d"
done

# ===========================================================================
# THE big win: strip debug / symbol info from all ELF files.
#
#   cc1 and cc1plus (the GCC C / C++ compiler internals) are typically
#   150-200 MB each when built with debug info. After --strip-all they
#   shrink to ~20-40 MB each — this single pass can cut 100+ MB from the
#   image. Every .elf app benefits too.
#
#   Static archives (.a) keep their symbol table (needed for linking) but
#   lose .debug_* sections via --strip-debug.
#
#   strip silently skips non-ELF files, so fonts / BMPs / WAVs / WADs are
#   never touched.
# ===========================================================================
if [[ "$STRIP_BINS" == "1" ]]; then
    # Prefer the cross toolchain's strip if available, then the host strip.
    strip_tool=""
    for candidate in x86_64-unknown-gaia-strip strip llvm-strip; do
        if command -v "$candidate" >/dev/null 2>&1; then
            strip_tool="$candidate"
            break
        fi
    done

    if [[ -n "$strip_tool" ]]; then
        # 1. Static archives & object files — strip debug only (keep symbols).
        if [[ "$STRIP_LIBS" == "1" ]]; then
            find "$staging" -type f \( -name '*.a' -o -name '*.o' \) -print0 \
                | xargs -0 -r "$strip_tool" --strip-debug 2>/dev/null || true
        fi

        # 2. Executables & shared libraries — strip everything possible.
        find "$staging" -type f \
            ! -name '*.a' ! -name '*.o' \
            -print0 \
            | xargs -0 -r "$strip_tool" --strip-all 2>/dev/null || true

        echo "create_disk: stripped ELF debug & symbol sections ($strip_tool)"
    else
        echo "create_disk: warning — 'strip' not found; skipping ELF strip" >&2
    fi
fi

# ===========================================================================
# Measure the staged payload
# ===========================================================================
payload_bytes="$(du -sb "$staging" | cut -f1)"
payload_mib=$((payload_bytes / 1024 / 1024))

if [[ "$VERBOSE" == "1" ]]; then
    echo "create_disk: staged payload breakdown (top 15 dirs):"
    du -sh "$staging"/* 2>/dev/null | sort -rh | head -15
fi

# ===========================================================================
# Compute image size
# ===========================================================================
if [[ -z "${DISK_SIZE_MIB:-}" ]]; then
    mib=$((1024 * 1024))
    slack_bytes=$((SLACK_MIB * mib))
    DISK_SIZE_MIB=$(((payload_bytes + slack_bytes + mib - 1) / mib))
    # FAT32 is only FAT32 above 65525 clusters — below that the driver reads
    # the geometry as FAT16 and refuses the volume, because a real FAT16 needs
    # the fixed root table this image would not have. At one 512-byte sector
    # per cluster that floor is 32 MiB; 64 gives room.
    ((DISK_SIZE_MIB < MIN_SIZE)) && DISK_SIZE_MIB=$MIN_SIZE
fi

echo "create_disk: payload ~${payload_mib} MiB -> image ${DISK_SIZE_MIB} MiB ($ROOTFS_TYPE)"

# ===========================================================================
# Create the image
# ===========================================================================
mkdir -p "$(dirname "$IMG")"

# truncate creates a sparse file instantly; dd is the portable fallback.
if command -v truncate >/dev/null 2>&1; then
    truncate -s "${DISK_SIZE_MIB}M" "$IMG"
else
    dd if=/dev/zero of="$IMG" bs=1M count="$DISK_SIZE_MIB" status=none
fi

if [[ "$ROOTFS_TYPE" == "ext2" ]]; then
    # Classic ext2 (no journal) with 1 KiB blocks. The backend also accepts
    # the revision-0 128-byte inode layout, but modern tools prefer 256.
    mke2fs -q -t ext2 -F -b 4096 -I 256 -d "$staging" "$IMG"
    echo "create_disk: validating ext2 filesystem..."

    e2fsck -fn "$IMG" || {
        echo "create_disk: ext2 validation failed" >&2
        exit 1
    }
    echo "$IMG created (${DISK_SIZE_MIB} MiB ext2)"
    # Verify essential boot executables.
    for path in /system/bin/sh.elf /system/bin/heimdall.elf; do
        if ! debugfs -R "stat $path" "$IMG" 2>&1 | grep -q 'Inode:'; then
            echo "create_disk: missing required file $path" >&2
            exit 1
        fi
    done
    exit 0
fi

# --- FAT32 ---
# -s 1 keeps one sector per cluster so the cluster count stays well clear
# of the FAT32 floor no matter how small the payload set gets.
mkfs.fat -F 32 -s 1 "$IMG" >/dev/null

# Copy the staged tree one top-level entry at a time. Each `mcopy -s`
# recurses internally, so this is dramatically faster than calling mcopy
# once per leaf file (hundreds of calls vs. a dozen).
for item in "$staging"/*; do
    [[ -e "$item" ]] || continue
    base="${item##*/}"
    mcopy -i "$IMG" -s "$item" "::$base"
done

# Guarantee reserved empty directories exist (mcopy may skip empties).
for fat_dir in bin mnt usr usr/bin usr/local usr/local/bin; do
    mdir -i "$IMG" "::$fat_dir" >/dev/null 2>&1 || mmd -i "$IMG" "::$fat_dir"
done

echo "$IMG created (${DISK_SIZE_MIB} MiB FAT32)"
