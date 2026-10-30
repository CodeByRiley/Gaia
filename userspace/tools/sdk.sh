#!/usr/bin/env bash
# userspace/tools/sdk.sh , check, install, and uninstall the staged libgaia SDK.
#
# Driven by build/sdk.mk, which passes everything through the environment:
#   SDK_CC SDK_NM       the Gaia toolchain
#   SDK_DIR             staged SDK (include/ and lib/libgaia.a)
#   SDK_INTREE_CFLAGS   flags the SDK libgaia objects were compiled with
#   SDK_LDSCRIPT        Gaia's user linker script
#   GAIA_SYSROOT        install destination
#
# Run from userspace/, as make does.
set -euo pipefail

die() {
	echo "sdk: $*" >&2
	exit 1
}

: "${SDK_CC:?}" "${SDK_NM:?}" "${SDK_DIR:?}"
include_dir="$SDK_DIR/include"
archive="$SDK_DIR/lib/libgaia.a"
manifest_rel="usr/share/gaia-sdk/manifest"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

check() {
	[[ -f "$archive" ]] || die "missing $archive; run make sdk"
	local failed=0

	# 1. Every public header compiles alone, twice over (include guard),
	#    with only the staged SDK and the compiler's sysroot on the path. A
	#    header that reached for lib/, arch/, or the source tree's include/
	#    fails here.
	local header name
	for header in "$include_dir"/gaia/*.h; do
		name=${header##*/}
		printf '#include <gaia/%s>\n#include <gaia/%s>\nint main(void) { return 0; }\n' \
			"$name" "$name" >"$work/standalone.c"
		if ! "$SDK_CC" -std=c11 -pedantic -Wall -Wextra -Werror -fsyntax-only \
			-I "$include_dir" "$work/standalone.c"; then
			echo "sdk: <gaia/$name> does not compile on its own" >&2
			failed=1
		fi
	done

	# 2. The in-tree declarations and the public ones agree. Conflicting
	#    redeclarations are a hard error in C, so one translation unit holding
	#    both proves a public prototype did not drift from the one libgaia was
	#    compiled against.
	printf '%s\n' '#include <lib/syscall.h>' '#include <lib/console.h>' \
		'#include <lib/process.h>' '#include <gaia/gaia.h>' >"$work/drift.c"
	# shellcheck disable=SC2086 # SDK_INTREE_CFLAGS is a flag list.
	if ! "$SDK_CC" $SDK_INTREE_CFLAGS -Werror -fsyntax-only "$work/drift.c"; then
		echo "sdk: public headers disagree with the in-tree declarations" >&2
		failed=1
	fi

	# 3. Every function the public headers declare is defined in the archive.
	#    -aux-info lists each prototype with the file that declared it, so the
	#    list cannot fall out of date the way a hand-kept one would.
	printf '#include <gaia/gaia.h>\n' >"$work/api.c"
	"$SDK_CC" -std=gnu11 -fsyntax-only -I "$include_dir" -aux-info "$work/api.aux" "$work/api.c"
	grep '/gaia/' "$work/api.aux" |
		sed -nE 's|^/\*[^*]*\*/ *extern [^(]*[ *]([A-Za-z_][A-Za-z0-9_]*) \(.*|\1|p' |
		sort -u >"$work/api.syms"
	[[ -s "$work/api.syms" ]] || die "found no declarations in the public headers"
	"$SDK_NM" -g --defined-only "$archive" | awk 'NF == 3 { print $3 }' | sort -u >"$work/gaia.syms"
	local missing
	missing=$(comm -23 "$work/api.syms" "$work/gaia.syms")
	if [[ -n "$missing" ]]; then
		echo "sdk: declared in <gaia/...> but not defined in libgaia.a:" >&2
		echo "$missing" | sed 's/^/  /' >&2
		failed=1
	fi

	# 4. libgaia defines nothing musl does. A duplicate would be resolved by
	#    archive order and could silently replace malloc or printf.
	local libc
	libc=$("$SDK_CC" -print-file-name=libc.a)
	[[ -f "$libc" ]] || die "cannot find the toolchain's libc.a"
	"$SDK_NM" -g --defined-only "$libc" 2>/dev/null | awk 'NF == 3 { print $3 }' | sort -u >"$work/libc.syms"
	local collisions
	collisions=$(comm -12 "$work/gaia.syms" "$work/libc.syms")
	if [[ -n "$collisions" ]]; then
		echo "sdk: libgaia.a defines symbols musl also defines:" >&2
		echo "$collisions" | sed 's/^/  /' >&2
		failed=1
	fi

	# 5. The whole archive links against musl and Gaia's linker script with
	#    nothing left unresolved. --whole-archive pulls in members a small
	#    test program would never reach.
	: "${SDK_LDSCRIPT:?}"
	printf 'int main(void) { return 0; }\n' >"$work/link.c"
	if ! "$SDK_CC" -T "$SDK_LDSCRIPT" "$work/link.c" \
		-Wl,--whole-archive "$archive" -Wl,--no-whole-archive -o "$work/link.elf"; then
		echo "sdk: libgaia.a does not link on its own" >&2
		failed=1
	fi

	((failed == 0)) || exit 1
	echo "sdk: $(wc -l <"$work/api.syms") public functions, $(wc -l <"$work/gaia.syms") archive symbols, no musl collisions"
}

sysroot() {
	[[ -n "${GAIA_SYSROOT:-}" ]] || die "GAIA_SYSROOT is unset and $SDK_CC reports no sysroot"
	[[ "$GAIA_SYSROOT" != "/" ]] || die "refusing to install into /"
	[[ -d "$GAIA_SYSROOT/usr/include" ]] || die "$GAIA_SYSROOT does not look like a sysroot (no usr/include)"
}

# Remove what the manifest lists and nothing else, then any directory the
# SDK alone created that is now empty. musl's headers share usr/include and
# usr/lib, so a blanket rm -r of either would take the C library with it.
remove_installed() {
	local manifest="$GAIA_SYSROOT/$manifest_rel" rel
	[[ -f "$manifest" ]] || return 0
	while IFS= read -r rel; do
		[[ -n "$rel" && "$rel" != /* && "$rel" != *..* ]] || die "bad manifest entry: $rel"
		rm -f -- "$GAIA_SYSROOT/$rel"
	done <"$manifest"
	rm -f -- "$manifest"
	rmdir -- "$GAIA_SYSROOT/usr/include/gaia/bits" "$GAIA_SYSROOT/usr/include/gaia" \
		"$GAIA_SYSROOT/usr/share/gaia-sdk" 2>/dev/null || true
}

install_sdk() {
	sysroot
	# Clear the previous install first so a header dropped from the SDK does
	# not linger in the sysroot.
	remove_installed
	local rel
	: >"$work/manifest"
	while IFS= read -r rel; do
		install -D -m 644 "$include_dir/$rel" "$GAIA_SYSROOT/usr/include/$rel"
		echo "usr/include/$rel" >>"$work/manifest"
	done < <(cd "$include_dir" && find gaia -type f -name '*.h' | sort)
	install -D -m 644 "$archive" "$GAIA_SYSROOT/usr/lib/libgaia.a"
	echo "usr/lib/libgaia.a" >>"$work/manifest"
	install -D -m 644 "$work/manifest" "$GAIA_SYSROOT/$manifest_rel"
	echo "sdk: installed $(wc -l <"$work/manifest") files into $GAIA_SYSROOT"
}

uninstall_sdk() {
	sysroot
	[[ -f "$GAIA_SYSROOT/$manifest_rel" ]] || {
		echo "sdk: nothing installed in $GAIA_SYSROOT"
		return 0
	}
	remove_installed
	echo "sdk: removed libgaia SDK from $GAIA_SYSROOT"
}

case "${1:-}" in
check) check ;;
install) install_sdk ;;
uninstall) uninstall_sdk ;;
*) die "usage: $0 check|install|uninstall" ;;
esac
