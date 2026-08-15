#!/bin/bash
# package.sh -- stage the app and build the .ipk by hand (ar + tar, no
# palm-package: its tar rebuild breaks on long paths -- see sdlquake notes).
#
# Layout on device: /media/cryptofs/apps/usr/palm/applications/<id>/
#   xash3d              launcher (appinfo "main" -- must be the real ELF for
#                       the LS2 role match; it chdirs via /proc/self/exe)
#   libxash.so, libref_gles1.so, libref_soft.so, libmenu.so,
#   filesystem_stdio.so
#   valve/extras.pk3    engine menu assets (works with no Valve data)
# Game data (Uplink/retail) goes to /media/internal/xash/valve/ -- see M3.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
source webos/env.sh

APPID=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' webos/app/appinfo.json)
VERSION=$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' webos/app/appinfo.json)
BUILD=xash3d-fwgs/build

STAGING=stage/ipk
APPDIR="$STAGING/usr/palm/applications/$APPID"
rm -rf "$STAGING"
mkdir -p "$APPDIR/valve" "$STAGING/CONTROL"

# binaries
cp "$BUILD/game_launch/xash3d" "$APPDIR/xash3d"
cp "$BUILD/engine/libxash.so" \
   "$BUILD/ref/gl/libref_gles1.so" \
   "$BUILD/ref/soft/libref_soft.so" \
   "$BUILD/3rdparty/mainui/libmenu.so" \
   "$BUILD/filesystem/filesystem_stdio.so" \
   "$APPDIR/"
$STRIP "$APPDIR"/xash3d "$APPDIR"/*.so 2>/dev/null || true
chmod 755 "$APPDIR/xash3d"

# engine menu assets -- boots to menu with zero Valve data
cp "$BUILD/3rdparty/extras/extras.pk3" "$APPDIR/valve/extras.pk3"
cp webos/app/valve/gameinfo.txt "$APPDIR/valve/"
cp webos/app/valve/pad_*.cfg "$APPDIR/valve/"
# network delta table (Valve SDK-published, same license basis as the hlsdk
# dlls) -- required at engine init, and the Uplink demo data lacks it
cp webos/app/valve/delta.lst "$APPDIR/valve/"

# Uplink demo data (Valve's freeware 1999 demo) -- a fresh install is a
# playable game out of the box. Lives in data/uplink-valve/ (gitignored;
# extraction from the demo installer is documented in docs/NOTES.md).
# A user's own data in /media/internal/xash/valve is searched first, so
# copied retail content overrides all of this per-file -- and the demo's
# Uplink maps remain available alongside retail as a bonus.
if [ -f data/uplink-valve/pak0.pak ]; then
    cp data/uplink-valve/pak0.pak data/uplink-valve/gfx.wad \
       data/uplink-valve/halflife.wad data/uplink-valve/cached.wad \
       "$APPDIR/valve/"
else
    echo "WARNING: data/uplink-valve/ missing -- building WITHOUT the bundled Uplink demo"
fi
# blank 128x128 charset satisfies the engine's hard gfx/conchars existence
# gate so a data-less install reaches the menu (console text uses the TTF
# fonts from extras.pk3; real game data overrides this file)
mkdir -p "$APPDIR/valve/gfx"
cp webos/app/valve/gfx/conchars "$APPDIR/valve/gfx/"
# GameUI strings for WON-era data (predates Steam's gameui_english.txt --
# without it the menu shows raw token names); Steam data overrides per-file
mkdir -p "$APPDIR/valve/resource"
cp webos/app/valve/resource/gameui_english.txt "$APPDIR/valve/resource/"

# ARM game libraries (GPL hlsdk-portable builds, no Valve content) -- users
# then only copy their valve/ data; FS searches the app dir (RoDir) after
# /media/internal, so copies in the data tree still override these
HLSDK=hlsdk-portable/build
mkdir -p "$APPDIR/valve/dlls" "$APPDIR/valve/cl_dlls"
cp "$HLSDK/dlls/hl_armv7l.so" "$APPDIR/valve/dlls/"
cp "$HLSDK/cl_dll/client_armv7l.so" "$APPDIR/valve/cl_dlls/"
$STRIP "$APPDIR/valve/dlls/hl_armv7l.so" "$APPDIR/valve/cl_dlls/client_armv7l.so" 2>/dev/null || true

# The engine prefers whichever game descriptor is NEWER (basedir liblist.gam/
# gameinfo.txt vs this shipped stub). A fresh install stamps now-mtimes, which
# silently re-points startmap at the stub's demo map until the user's data is
# touched again -- date the stub at the epoch so user data always wins.
touch -t 197001020000 "$APPDIR/valve/gameinfo.txt"

# app metadata (NO metadata.json -- it forces 320x480 phone-compat mode)
cp webos/app/appinfo.json "$APPDIR/"
[ -f webos/app/icon.png ] && cp webos/app/icon.png "$APPDIR/"

# ABI gate
scripts/check-symbols.sh "$APPDIR/xash3d" "$APPDIR"/*.so \
    "$APPDIR/valve/dlls/hl_armv7l.so" "$APPDIR/valve/cl_dlls/client_armv7l.so"

# control
cat > "$STAGING/CONTROL/control" <<EOF
Package: $APPID
Version: $VERSION
Section: Games
Priority: optional
Architecture: arm
Installed-Size: $(du -sk "$STAGING/usr" | cut -f1)
Maintainer: webOS Archive <support@webosarchive.org>
Description: Half-Life via Xash3D FWGS for the HP TouchPad
EOF
for s in postinst prerm; do
    if [ -f "webos/app/control/$s" ]; then
        cp "webos/app/control/$s" "$STAGING/CONTROL/$s"
        chmod 755 "$STAGING/CONTROL/$s"
    fi
done

# assemble ipk
mkdir -p ipks
OUT="$ROOT/ipks/${APPID}_${VERSION}_armv7.ipk"
( cd "$STAGING" &&
  tar czf data.tar.gz ./usr &&
  tar czf control.tar.gz -C CONTROL . &&
  echo "2.0" > debian-binary &&
  rm -f "$OUT" &&
  ar -cr "$OUT" debian-binary control.tar.gz data.tar.gz )
echo "built $OUT"
