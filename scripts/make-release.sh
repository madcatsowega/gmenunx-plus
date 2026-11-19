#!/bin/sh
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$REPO_ROOT/dist/retrofw"
STAGE="/tmp/gmenunx-plus-release"
OUT_ZIP="$REPO_ROOT/dist/gmenunx-plus-retrofw2.3.zip"

if [ ! -f "$DIST/gmenunx" ] || [ ! -f "$DIST/libperfhud.so" ]; then
	echo "Missing dist/retrofw/gmenunx or libperfhud.so. Build first:" >&2
	echo "  source ~/retrofw-dev/env.sh && make PLATFORM=retrofw CC=mipsel-linux-gcc CXX=mipsel-linux-g++ STRIP=mipsel-linux-strip dist" >&2
	exit 1
fi

rm -rf "$STAGE" "$OUT_ZIP"
mkdir -p "$STAGE/apps/gmenu2x-plus"

cp -r "$DIST/gmenunx" "$DIST/libperfhud.so" "$DIST/about.txt" "$DIST/COPYING" "$DIST/input.conf" "$DIST/skins" "$DIST/translations" "$STAGE/apps/gmenu2x-plus/"
cp "$REPO_ROOT/README.md" "$REPO_ROOT/ROADMAP.md" "$STAGE/"

cat << 'EOF' > "$STAGE/autoexec.sh.sample"
#!/bin/sh
export HOME=/home/retrofw
# Change GMENUNX_HOME to /home/retrofw/.gmenunx if you use stock RetroFW 2.3 .gmenunx links,
# or /home/retrofw/apps/gmenu2x if your links & skins live in apps/gmenu2x.
export GMENUNX_HOME=/home/retrofw/.gmenunx
export GMENUNX_DATA=/home/retrofw/apps/gmenu2x-plus

# Safe per-boot SD I/O & VM tuning
if [ ! -f /tmp/.rfw_tuned ]; then
	for dev in /sys/block/mmcblk*; do
		[ -w "$dev/queue/scheduler" ] && echo noop > "$dev/queue/scheduler" 2>/dev/null
		[ -w "$dev/queue/read_ahead_kb" ] && echo 256 > "$dev/queue/read_ahead_kb" 2>/dev/null
	done
	[ -w /proc/sys/vm/swappiness ] && echo 20 > /proc/sys/vm/swappiness 2>/dev/null
	[ -w /proc/sys/vm/vfs_cache_pressure ] && echo 50 > /proc/sys/vm/vfs_cache_pressure 2>/dev/null
	touch /tmp/.rfw_tuned
fi

if [ -f /home/retrofw/apps/gmenu2x-plus/gmenunx ]; then
	if cp /home/retrofw/apps/gmenu2x-plus/gmenunx /tmp/gmenunx-plus && chmod 755 /tmp/gmenunx-plus; then
		cd /home/retrofw/apps/gmenu2x-plus
		exec /tmp/gmenunx-plus
	fi
fi

exec /usr/bin/gmenunx
EOF

cat << 'EOF' > "$STAGE/INSTALL.txt"
GMenuNX+ Modern (RetroFW 2.3) - Non-Destructive Install
=======================================================

1. Copy the `apps/gmenu2x-plus` folder onto the root of your `RETROFW` partition
   (so you have `/home/retrofw/apps/gmenu2x-plus/gmenunx` on the device).

2. Back up any existing `autoexec.sh` on the root of your `RETROFW` partition,
   then copy `autoexec.sh.sample` to `autoexec.sh`.
   - If your links & skins live in `.gmenunx/` (stock RetroFW 2.3), keep:
       export GMENUNX_HOME=/home/retrofw/.gmenunx
   - If your links & skins live in `apps/gmenu2x/`, change it to:
       export GMENUNX_HOME=/home/retrofw/apps/gmenu2x

3. Eject safely and boot your handheld! Check Settings -> About to confirm
   "GMenuNX+ Modern" is running.

Uninstall:
  Delete `autoexec.sh` (or restore your backup) and delete `apps/gmenu2x-plus`.
  Your stock `rootfs.squashfs` is never modified!
EOF

(cd "$STAGE" && zip -qr "$OUT_ZIP" .)
rm -rf "$STAGE"
ls -lh "$OUT_ZIP"
