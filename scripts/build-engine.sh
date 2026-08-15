#!/bin/bash
# build-engine.sh -- cross-configure + build Xash3D FWGS for the TouchPad.
#
#   scripts/build-engine.sh configure   # (re)run waf configure
#   scripts/build-engine.sh            # build (configures first if needed)
#   scripts/build-engine.sh clean      # waf clean
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
source webos/env.sh

cd xash3d-fwgs

# waf reads toolchain from env. Keep --sysroot in the FLAGS (not CC) so waf's
# compiler detection sees a plain executable path.
export CC="${CROSS_PREFIX}gcc"
export CXX="${CROSS_PREFIX}g++"
export AR RANLIB STRIP
SYSROOT_FLAG="--sysroot=$WEBOS_SYSROOT"
COMPAT="-include $ROOT/webos/glibc25-math-compat.h"

# -std=gnu11: gcc 4.9 defaults to gnu90; the engine assumes gcc>=5's gnu11 default
# -fgnu89-inline: glibc-2.5 headers use GNU89 'extern inline' semantics; under
#   C99 inline rules their stat64/lstat64 stubs get emitted twice (asm error
#   "symbol stat64 is already defined" in any TU with _FILE_OFFSET_BITS=64)
# -Wno-error=strict-aliasing: gcc 4.9 flags type-puns in ref_soft that newer
#   gcc (upstream's CI) accepts; the wscript's -Werror list predates 4.9
# app version for the App Museum update check (updater_webos.c) -- parsed
# from appinfo.json so the two can never drift apart. Unquoted on purpose:
# CFLAGS is expanded unquoted and a quoted string would not survive
# word-splitting; the stub stringifies it.
APP_VERSION=$(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$ROOT/webos/app/appinfo.json")

export CFLAGS="$SYSROOT_FLAG $WEBOS_OPTS -std=gnu11 -fgnu89-inline -Wno-error=strict-aliasing -D__webos__ -DXASHHL_VERSION_RAW=$APP_VERSION"
export CXXFLAGS="$SYSROOT_FLAG $WEBOS_OPTS -D__webos__ $COMPAT"
# -static-libstdc++: Linaro's shared libstdc++ needs GLIBC_2.17 clock_gettime;
# statically linked it resolves clock_gettime from the sysroot's librt (D5).
# -static-libgcc + -lgcc_eh: libbacktrace needs _Unwind_Backtrace; the 2011
# device libgcc_s may not export it, so carry the unwinder statically.
export LINKFLAGS="$SYSROOT_FLAG -static-libstdc++ -static-libgcc -lgcc_eh -lrt"
export LDFLAGS="$SYSROOT_FLAG -static-libstdc++ -static-libgcc -lgcc_eh -lrt"
export PKG_CONFIG_LIBDIR="$ROOT/webos/pkgconfig"

case "${1:-build}" in
configure)
    ./waf configure -T release \
        --use-sdl1 \
        --enable-gles1 \
        --disable-gl \
        --enable-stbtt \
        --disable-mbedtls \
        --prefix=/
    ;;
build)
    [ -d build ] || "$0" configure
    ./waf build -j"$(nproc)"
    ;;
clean)
    ./waf clean
    ;;
*)
    echo "usage: $0 [configure|build|clean]" >&2; exit 1;;
esac
