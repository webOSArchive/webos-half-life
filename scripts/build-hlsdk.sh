#!/bin/bash
# build-hlsdk.sh -- cross-build hlsdk-portable (hl_armv7l.so game logic +
# client_armv7l.so client dll) for the TouchPad. Same toolchain recipe as the
# engine (see build-engine.sh).
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
source webos/env.sh

cd hlsdk-portable

export CC=/home/jonwise/linaro-toolchain/bin/arm-linux-gnueabi-gcc
export CXX=/home/jonwise/linaro-toolchain/bin/arm-linux-gnueabi-g++
export AR RANLIB STRIP
SYSROOT_FLAG="--sysroot=$WEBOS_SYSROOT"
COMPAT="-include $ROOT/webos/glibc25-math-compat.h"

export CFLAGS="$SYSROOT_FLAG $WEBOS_OPTS -std=gnu11 -fgnu89-inline -D__webos__"
export CXXFLAGS="$SYSROOT_FLAG $WEBOS_OPTS -D__webos__ $COMPAT"
export LINKFLAGS="$SYSROOT_FLAG -static-libstdc++ -static-libgcc -lgcc_eh -lrt"
export LDFLAGS="$LINKFLAGS"

case "${1:-build}" in
configure)
    ./waf configure -T release --disable-goldsrc-support --disable-werror
    ;;
build)
    [ -d build ] || "$0" configure
    ./waf build -j"$(nproc)"
    ;;
clean) ./waf clean ;;
*) echo "usage: $0 [configure|build|clean]" >&2; exit 1 ;;
esac
