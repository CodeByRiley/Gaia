# Userspace application manifests

Add a directory under `userspace/bin/` with a `build.mk` and its sources:

```make
APP_FORMAT := both
APP_RUNTIME := musl
APP_SOURCES := main.c render.c
APP_LIBS := gaia
APP_CFLAGS := -DMY_FEATURE
APP_LDFLAGS :=
```

Run `mingw32-make -j12 userspace` from the repository root (or `make` on
Linux with the cross toolchains installed). No central application list changes
are needed. Existing executable destinations remain `bin/<directory>/<name>.elf`
and `.exe`, so the loaders and root filesystem paths stay compatible.

`APP_NAME` defaults to the directory name and must be unique, using letters,
digits, underscores or hyphens, with no initial hyphen. `APP_FORMAT` defaults to
`elf` and accepts `elf`, `pe` or `both`. Source paths are relative to the app
directory; C, preprocessed `.S`, and NASM `.s`/`.asm` sources are supported.
Absolute paths and parent traversal are rejected before any build commands run.
NASM sources must use the ABI of each requested format; sharing a source does
not automatically translate calling conventions. NASM include dependencies are
not compiler-generated; list such dependencies explicitly if needed.

ELF defaults to musl; PE defaults to the native runtime. `APP_ELF_RUNTIME` and
`APP_PE_RUNTIME` override each format independently. In a `both` manifest,
`APP_RUNTIME` controls ELF and PE defaults to native. PE with musl is rejected.
The native `thread` program retains Gaia's thread syscalls. The self-contained
`pe_test` declares an empty `APP_LIBS` to retain its own entry point and stubs.

The library registry currently exposes `gaia`, resolved differently for each
format/runtime. Omitting `APP_LIBS` selects it; explicitly leaving it empty
selects no Gaia library. musl startup and libc ordering still belong to
`libc/link_gaia_musl.sh`. Add future named libraries to `build/runtimes.mk`;
application manifests cannot pass arbitrary library filenames via `APP_LIBS`.

Each manifest is reset before loading and its rules capture concrete values.
This prevents options from leaking into the next app during Make's `eval`
expansion. Objects retain their complete source filename and live in separate
repository-level `build/userspace/elf` and `pe` trees. Shared runtime objects
also have distinct native/musl trees. Compiler dependency files track C and
preprocessed assembly headers. Manifest/build-rule edits invalidate affected
objects and links; ordinary source edits rebuild only their translation unit.

HolyD, DOOM and NetSurf deliberately retain explicit source/build rules and
existing special build directories. DOOM/NetSurf remain optional through
`BUILD_DOOM=1` / `BUILD_NETSURF=1`. Migrating another conventional app means
adding its manifest and removing any old explicit rule for the same output.

The top-level userspace target writes `build/userspace/targets.list` after a
successful build. Disk packaging reads that list, retaining existing system-app
destinations and installing other registered executables in `/usr/bin`.
Unregistered or disabled binaries left on disk are excluded. Direct callers of
`tools/create_disk.sh` must first run the userspace build; direct sub-build
callers can use `make -C userspace package-list`.

`make -C userspace clean` removes registered executables, the new output tree,
and explicitly known special-program outputs. It leaves manifests, sources,
checked-out submodules and the configured musl toolchain intact. Older in-source
objects from before this migration are unused and can be removed separately.

Verification:

- `python3 tests/userspace_build_test.py`: isolated manifest/error/incremental tests
  (set `GAIA_TEST_MAKE=mingw32-make` when needed).
- `mingw32-make -j12 userspace`: parallel application build.
- `make -C userspace -q all`: exits zero when no compilation/linking is needed.
- `mingw32-make -j12 build-x86_64`: disk image and BIOS/UEFI ISO.
- `python3 tests/userspace_formats_test.py` in WSL/Linux: boots the ISO and runs
  ELF hello, PE hello, self-contained pe_test, and PE ls.
- Existing `test-host` and QEMU integration tests remain available unchanged.
