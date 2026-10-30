#!/usr/bin/env bash
# tools/stage_native_sysroot.sh , copy the link-time runtime into rootfs/.
#
# The native GCC under rootfs/system is configured --prefix=/system with no
# sysroot, so inside Gaia it looks for its startup files and libraries where
# that prefix puts them:
#
#   /system/lib/                                crt1.o crti.o crtn.o libc.a ...
#   /system/lib/gcc/x86_64-unknown-gaia/17.0.0/ libgcc.a
#   /system/include/                            musl's headers
#
# (/system/include is the prefix's include dir; cc1 searches it after GCC's
# own include and include-fixed.)
#
# Those are built on the host by the cross toolchain (musl into its sysroot,
# libgcc by GCC itself) and are target code, so the same files serve the
# native compiler. This copies them into rootfs/system/lib, from where
# tools/create_disk.sh and tools/update_disk.py package them.
#
# Run under WSL. GAIA_TOOLCHAIN_PREFIX overrides the toolchain location.
set -euo pipefail

cd "$(dirname "$0")/.."

prefix="${GAIA_TOOLCHAIN_PREFIX:-$HOME/opt/gaia}"
target=x86_64-unknown-gaia
version=17.0.0
sysroot_lib="$prefix/$target/sysroot/usr/lib"
sysroot_include="$prefix/$target/sysroot/usr/include"
gcc_lib="$prefix/lib/gcc/$target/$version"

out_lib=rootfs/system/lib
out_gcc="$out_lib/gcc/$target/$version"
mkdir -p "$out_lib" "$out_gcc"

for f in crt1.o crti.o crtn.o libc.a libm.a libgaia.a gaia.ld; do
    [[ -f "$sysroot_lib/$f" ]] || { echo "stage_native_sysroot: missing $sysroot_lib/$f" >&2; exit 1; }
    install -m 0644 "$sysroot_lib/$f" "$out_lib/$f"
done
for f in libgcc.a libgcov.a; do
    [[ -f "$gcc_lib/$f" ]] || { echo "stage_native_sysroot: missing $gcc_lib/$f" >&2; exit 1; }
    install -m 0644 "$gcc_lib/$f" "$out_gcc/$f"
done

[[ -f "$sysroot_include/stdio.h" ]] || { echo "stage_native_sysroot: no headers in $sysroot_include" >&2; exit 1; }
rm -rf rootfs/system/include
mkdir -p rootfs/system/include
cp -R "$sysroot_include/." rootfs/system/include/

echo "stage_native_sysroot: staged runtime from $prefix into $out_lib"
