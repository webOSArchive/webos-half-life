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

# app metadata (NO metadata.json -- it forces 320x480 phone-compat mode)
cp webos/app/appinfo.json "$APPDIR/"
[ -f webos/app/icon.png ] && cp webos/app/icon.png "$APPDIR/"

# ABI gate
scripts/check-symbols.sh "$APPDIR/xash3d" "$APPDIR"/*.so

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
