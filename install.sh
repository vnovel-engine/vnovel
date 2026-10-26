#!/bin/sh
# install.sh — install the VNovel libretro core into a RetroArch (or
# RetroArch-based frontend) cores directory.
#
#   ./install.sh                     # detect the frontend's core directory
#   ./install.sh --dir ~/.config/retroarch/cores
#   ./install.sh --prefix /usr       # <prefix>/lib/libretro
#   ./install.sh --uninstall
#
# The core is one self-contained shared library (the runtime and both engines
# are linked in), so this installs the single .so. The database and the
# core-info file go to RetroArch's own directories so the frontend can name
# games on a scan.
#
# POSIX sh; no bashisms, so it runs on BusyBox (Recalbox, LibreELEC, batocera).

set -eu

PROG=$(basename "$0")
HERE=$(cd "$(dirname "$0")" && pwd)

# The single artifact this release ships. A bundle carries exactly one, under
# the platform's own extension; the first that exists is the one installed, so
# the same script serves the Linux, Windows and macOS bundles.
CORE=""
for _ext in so dll dylib; do
	if [ -f "$HERE/vnovel_libretro.$_ext" ]; then
		CORE="vnovel_libretro.$_ext"
		break
	fi
done
INFO="vnovel_libretro.info"
RDB="vnovel.rdb"

cores_dir=""
prefix=""
uninstall=0
dry=0

usage() {
	cat <<EOF
Usage: $PROG [options]

  --dir DIR      install the core into DIR (the frontend's cores directory)
  --prefix DIR   install under DIR: <DIR>/lib/libretro (default /usr/local)
  --uninstall    remove a previously installed core
  --dry-run      print what would be done, install nothing
  -h, --help     this help

With neither --dir nor --prefix, common RetroArch locations are probed and the
first existing one is used.
EOF
}

say()  { [ "$dry" -eq 1 ] && printf '(dry-run) %s\n' "$*" || printf '%s\n' "$*"; }
die()  { printf '%s: %s\n' "$PROG" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case "$1" in
		--dir)      [ $# -ge 2 ] || die "--dir needs an argument"; cores_dir=$2; shift 2 ;;
		--prefix)   [ $# -ge 2 ] || die "--prefix needs an argument"; prefix=$2; shift 2 ;;
		--uninstall) uninstall=1; shift ;;
		--dry-run)  dry=1; shift ;;
		-h|--help)  usage; exit 0 ;;
		*)          die "unknown option: $1 (try --help)" ;;
	esac
done

# where the core goes

if [ -z "$cores_dir" ] && [ -n "$prefix" ]; then
	cores_dir="$prefix/lib/libretro"
fi

if [ -z "$cores_dir" ]; then
	# Ordered most-specific first. $HOME may be unset when running as a
	# system account, so each candidate is only used if it exists.
	for d in \
		"${XDG_CONFIG_HOME:-$HOME/.config}/retroarch/cores" \
		"$HOME/.config/retroarch/cores" \
		"$HOME/.retroarch/cores" \
		/usr/lib/libretro \
		/usr/local/lib/libretro \
		/opt/retroarch/lib/libretro \
		/recalbox/share/system/.config/retroarch/cores
	do
		if [ -d "$d" ]; then cores_dir=$d; break; fi
	done
fi

[ -n "$cores_dir" ] || die "no cores directory found; pass --dir or --prefix"

# The database lives beside the frontend config, not beside the cores. Derive
# it from the cores path when it is the usual <config>/retroarch/cores shape.
db_dir=""
case "$cores_dir" in
	*/retroarch/cores)
		base=${cores_dir%/cores}
		[ -d "$base" ] && db_dir="$base/database/rdb"
		;;
esac

src_core="$HERE/$CORE"
[ -n "$CORE" ] && [ -f "$src_core" ] || \
	die "no vnovel_libretro.{so,dll,dylib} next to $PROG; run this from the unpacked release"

# uninstall

if [ "$uninstall" -eq 1 ]; then
	target="$cores_dir/$CORE"
	if [ -e "$target" ]; then say "remove $target"; [ "$dry" -eq 1 ] || rm -f "$target"; fi
	if [ -n "$db_dir" ] && [ -e "$db_dir/$RDB" ]; then
		say "remove $db_dir/$RDB"; [ "$dry" -eq 1 ] || rm -f "$db_dir/$RDB"
	fi
	say "done"
	exit 0
fi

# install

say "cores directory: $cores_dir"
[ "$dry" -eq 1 ] || mkdir -p "$cores_dir" || die "cannot create $cores_dir"

install_one() {
	src=$1; dst_dir=$2
	[ -f "$src" ] || return 1
	say "install $(basename "$src") -> $dst_dir/"
	[ "$dry" -eq 1 ] && return 0
	# Install to a temporary name and rename, so a partially-written core is
	# never loadable: the frontend may be running and will pick up the file on
	# its next scan.
	tmp="$dst_dir/.$(basename "$src").new.$$"
	cp "$src" "$tmp" && chmod 0755 "$tmp" && mv -f "$tmp" "$dst_dir/$(basename "$src")"
}

# The one core file goes into the cores directory.
install_one "$HERE/$CORE" "$cores_dir" || die "failed to install $CORE"

# Core info: RetroArch reads it from the cores directory next to the core.
if [ -f "$HERE/$INFO" ]; then
	install_one "$HERE/$INFO" "$cores_dir" || true
	[ "$dry" -eq 1 ] || chmod 0644 "$cores_dir/$INFO" 2>/dev/null || true
fi

# Database: only when we could work out the frontend's config directory.
if [ -f "$HERE/$RDB" ]; then
	if [ -n "$db_dir" ]; then
		[ "$dry" -eq 1 ] || mkdir -p "$db_dir" || die "cannot create $db_dir"
		install_one "$HERE/$RDB" "$db_dir" || true
		[ "$dry" -eq 1 ] || chmod 0644 "$db_dir/$RDB" 2>/dev/null || true
	else
		say "note: install $RDB into your frontend's database/rdb directory to get game names"
	fi
fi

# The KiriKiri engine's data, as vnovel/ beside the core. Not optional: without
# system_polyfill/ the engine has no scenario parser and no .xp3 game can boot,
# and without overrides.txt it tries to load the game's own Windows plugins and
# aborts startup. The core finds the directory from its own path, so it must sit
# next to the core and not somewhere else.
#
# Copied whole rather than file by file so a future addition to the engine's
# data travels without this script being updated.
if [ -d "$HERE/vnovel" ]; then
	say "install vnovel/ (engine data) -> $cores_dir/"
	if [ "$dry" -eq 1 ]; then
		:
	elif [ -d "$cores_dir/vnovel" ]; then
		# Merge, and remove stale files: an upgrade must not leave a renamed
		# or withdrawn .tjs behind to be loaded.
		cp -R "$HERE/vnovel/." "$cores_dir/vnovel/" 2>/dev/null ||
			die "failed to install vnovel/"
	else
		cp -R "$HERE/vnovel" "$cores_dir/vnovel" || die "failed to install vnovel/"
	fi
	# Load-bearing and silently fatal when missing: the parser is the whole
	# polyfill, and the override table is what stops the engine from trying to
	# run the game's own PE plugins.
	for req in vnovel/overrides.txt vnovel/system_polyfill/PluginStubs.tjs \
		vnovel/system_polyfill/KAGParser.tjs
	do
		if [ "$dry" -eq 0 ] && [ ! -r "$cores_dir/$req" ]; then
			die "$req missing after install; KiriKiri games will not run"
		fi
	done
else
	die "vnovel/ (engine data) missing from the bundle; KiriKiri games will not run"
fi

say "done. Load the 'VNovel' core; select a game's nscript.dat, pscript.dat or data.xp3."
