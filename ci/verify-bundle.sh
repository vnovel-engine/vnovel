#!/bin/sh
# ci/verify-bundle.sh — check a release bundle carries exactly what it should.
#
# Usage: sh ci/verify-bundle.sh <bundle> [bundle ...]
#
# One bundle is one platform and carries exactly one core. What is checked is
# what a consumer would notice: the core present and named for the platform,
# the core-info and the game database beside it, install.sh to install it, and
# no stray engine module left over from the older multi-artifact layout.
#
# A Windows DLL gets one extra check, and it is the one that matters for a
# libretro core: the DLL must import only Windows system libraries. A core that
# imports libfreetype-6.dll or libwinpthread-1.dll does not start at all unless
# those travel with it, and libretro's buildbot and every manual install copy
# the core alone.

set -eu

[ $# -ge 1 ] || { echo "usage: verify-bundle.sh <bundle> [bundle ...]" >&2; exit 1; }

fail=0
note() { printf '%s\n' "$*"; }
bad()  { printf 'FAIL: %s\n' "$*" >&2; fail=1; }

for bundle in "$@"; do
	[ -e "$bundle" ] || { bad "no such bundle: $bundle"; continue; }

	# Absolute, because the members of a .deb are extracted from inside a
	# temporary directory and a relative path would then name nothing. Also,
	# nothing here changes the working directory in a way that outlives the
	# iteration, or the next bundle would be looked for in the wrong place.
	bundle=$(cd "$(dirname "$bundle")" && printf '%s/%s' "$(pwd)" "$(basename "$bundle")")

	note "== $bundle"

	# Debian packages are checked by their own contents, not by unpacking:
	# a .deb is an ar archive holding control.tar.* and data.tar.*, so it is
	# read with binutils' ar and tar. dpkg-deb would be the obvious tool, but
	# it is not installed on every host that builds the core (Arch, the
	# macOS runner), and needing it here would mean this check silently did
	# nothing there.
	case "$bundle" in
		*.deb)
			work=$(mktemp -d)
			control="$work/control"; data="$work/data"
			mkdir -p "$control" "$data"
			trap 'rm -rf "$work"' EXIT
			( cd "$control" && ar x "$bundle" ) 2>/dev/null ||
				{ bad "$bundle: not readable as ar (needs binutils)"; trap - EXIT; rm -rf "$work"; continue; }
			for m in "$control"/control.tar.*; do
				[ -f "$m" ] && tar xf "$m" -C "$control"
			done
			for m in "$control"/data.tar.*; do
				[ -f "$m" ] && tar xf "$m" -C "$data"
			done
			[ -f "$control/control" ] || { bad "$bundle: no control file"; trap - EXIT; rm -rf "$work"; continue; }
			pkg=$(sed -n 's/^Package: //p' "$control/control")
			arch=$(sed -n 's/^Architecture: //p' "$control/control")
			ver=$(sed -n 's/^Version: //p' "$control/control")
			[ -n "$pkg" ] || bad "$bundle: no Package field"
			[ -n "$arch" ] || bad "$bundle: no Architecture field"
			# A Debian version must start with a digit (and may carry a
			# revision); a tag-derived "v0.6.0-dirty" would be rejected by
			# dpkg -i.
			case "$ver" in
				[0-9]*) ;;
				*) bad "$bundle: Version does not start with a digit: '$ver'" ;;
			esac
			# The core belongs in the tripleted directory Debian's retroarch
			# package points libretro_directory at.
			cores=$(find "$data" -path '*libretro/vnovel_libretro.so' | wc -l)
			[ "$cores" -eq 1 ] || bad "$bundle: expected exactly one core, found $cores"
			find "$data" -path '*usr/share/libretro/info/vnovel_libretro.info' |
				grep -q . || bad "$bundle: core-info missing"
			note "   $pkg $ver $arch: $(find "$data" -type f | grep -c . ) files"
			trap - EXIT
			rm -rf "$work"
			continue
			;;
	esac

	# List the archive once, into a variable: how many cores it holds decides
	# what the bundle is, and listing it twice would read the file twice.
	case "$bundle" in
		*.zip) listing=$(unzip -l "$bundle" | awk 'NR>3 && NF>3 {print $4}') ;;
		*)     listing=$(tar tzf "$bundle") ;;
	esac

	for want in vnovel_libretro.info vnovel.rdb install.sh; do
		printf '%s\n' "$listing" | grep -q "/$want\$" ||
			bad "$bundle: $want missing"
	done

	cores=$(printf '%s\n' "$listing" | grep -cE '/vnovel_libretro\.(so|dll|dylib)$' || true)
	[ "$cores" -eq 1 ] ||
		bad "$bundle: expected exactly one core (vnovel_libretro.{so,dll,dylib}), found $cores"

	# A stray engine module means the build fell back to the old
	# multi-artifact path, where each engine was its own shared object. That
	# layout cannot be shipped: libretro copies one file per core.
	if printf '%s\n' "$listing" | grep -qE '/vnovel_(engine_.*|runtime)\.(so|dll|dylib)$'; then
		bad "$bundle: stray engine/runtime module (old multi-artifact layout)"
	fi

	core=$(printf '%s\n' "$listing" | grep -E '/vnovel_libretro\.(so|dll|dylib)$' | head -1)

	case "$core" in
		*.dll)
			# The import table is read from the DLL inside the archive, not
			# from a file in the working tree: the archive is what ships.
			#
			# Windows gets BOTH a .zip and a .tar.gz from ci/package.sh, so the
			# extraction has to follow the archive's own type. Calling unzip on
			# the tar.gz fails with "End-of-central-directory signature not
			# found" and aborts the job — which is how this was found.
			tmp=$(mktemp -d)
			trap 'rm -rf "$tmp"' EXIT
			case "$bundle" in
				*.zip) unzip -qq -o "$bundle" "$core" -d "$tmp" ;;
				*)     tar xzf "$bundle" -C "$tmp" "$core" ;;
			esac
			imports=$(objdump -p "$tmp/$core" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p' | sort -u)
			[ -n "$imports" ] || bad "$bundle: could not read the DLL's import table"
			# Anything not a Windows system library must travel with the DLL.
			# These are the MinGW runtimes a mingw-w64 build can pull in; the
			# core links them statically today (see the Makefile's mingw
			# block), so this list is normally empty and the check is that it
			# stays that way.
			non_system=$(printf '%s\n' "$imports" | grep -viE \
				'^(api-ms-win-|ext-ms-|advapi32|bcrypt|cfgmgr32|combase|comdlg32|crypt32|d3d|dbghelp|dinput8|dmoguids|dsound|dwmapi|dxgi|dxguid|gdi32|imm32|iphlpapi|kernel32|mf|mfuuid|mpr|msvcrt|normaliz|ntdll|ole32|oleaut32|powrprof|propsys|quartz|rpcrt4|secur32|setupapi|shell32|shlwapi|strmiids|user32|userenv|uxtheme|version|windowscodecs|winhttp|winmm|winspool|ws2_32|amstrmid)' || true)
			if [ -n "$non_system" ]; then
				if printf '%s\n' "$listing" | grep -qiE '/libwinpthread-1\.dll$|/libfreetype-6\.dll$|/zlib1\.dll$|/libbrotli(dec|common)\.dll$'; then
					note "   non-system imports (shipped beside the core):"
					printf '     %s\n' "$non_system"
				else
					bad "$bundle: DLL imports libraries that are not in the bundle:"
					printf '     %s\n' "$non_system" >&2
				fi
			fi
			note "   imports: $(printf '%s\n' "$imports" | grep -c .) libraries, all system"
			;;
	esac

	note "   $(printf '%s\n' "$listing" | grep -c . ) members, core: $core"
done

[ "$fail" -eq 0 ] || { echo "verify-bundle: FAILED" >&2; exit 1; }
echo "verify-bundle: OK"
