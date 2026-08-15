# webos/env.sh -- cross-compilation environment for the webOS TouchPad GoldSrc port.
# Source this from every build script:  source "$(dirname "$0")/../webos/env.sh"
#
# TOOLCHAIN CHOICE (Decision D1, RESOLVED 2026-07-31 -- see docs/NOTES.md):
#   Xash3D FWGS uses C11 duplicate typedefs pervasively, which gcc 4.3.3
#   rejects (legal from gcc ~4.6). So the engine compiler is Linaro 4.9.4
#   pointed at the PDK's glibc-2.5 SYSROOT -- that combination emits only
#   GLIBC_2.4 symbols and the binary runs on device (verified: hello+libm).
#   Linaro WITHOUT the sysroot links libm against GLIBC_2.15 and will not load.
#   The PDK's own gcc 4.3.3 (arm-none-linux-gnueabi-) remains for the small
#   plain-C diag tools (build-diag.sh sets it locally).
#
# C++ NOTE (D5): Linaro 4.9's libstdc++ is newer than the device's 6.0.9.
#   Link C++ modules with -static-libstdc++ (preferred) or bundle Linaro's
#   libstdc++.so.6 in the app dir with rpath $ORIGIN.
#
# FLAG RULES (all learned on hardware, do not "clean up"):
#   - NO -fsigned-char  : 1990s engines have char-indexed tables; ARM's default
#                         unsigned char is what they shipped with
#   - softfp, not hard  : the whole PDK ABI is softfp
#   - -funroll-loops ICEs the PDK gcc 4.3.3 (diag tools); Linaro is fine but
#     don't add it anyway -- keep flags identical across compilers

# Override with env vars for non-default install locations:
#   PALM_PDK          Palm PDK root         (default /opt/PalmPDK)
#   LINARO_TOOLCHAIN  Linaro GCC 4.9.4-2017.01 arm-linux-gnueabi root
#                     (default ~/linaro-toolchain; see README Prerequisites)
PDK="${PALM_PDK:-/opt/PalmPDK}"
export WEBOS_SYSROOT="$PDK/arm-gcc/sysroot"

LINARO_BIN="${LINARO_TOOLCHAIN:-$HOME/linaro-toolchain}/bin"
export CROSS_PREFIX="$LINARO_BIN/arm-linux-gnueabi-"

export CC="${CROSS_PREFIX}gcc --sysroot=$WEBOS_SYSROOT"
export CXX="${CROSS_PREFIX}g++ --sysroot=$WEBOS_SYSROOT"
export AR="${CROSS_PREFIX}ar"
export RANLIB="${CROSS_PREFIX}ranlib"
export STRIP="${CROSS_PREFIX}strip"
# readelf from the PDK works on everything and is always present
export READELF="$PDK/arm-gcc/bin/arm-none-linux-gnueabi-readelf"

# PDK gcc for the plain-C diag tools (proven combination, keep unchanged)
export PDK_CC="$PDK/arm-gcc/bin/arm-none-linux-gnueabi-gcc"

# Optimization + ABI. -ffast-math/-fsingle-precision-constant matter on a
# Cortex-A8 whose double-precision VFP is not pipelined.
export WEBOS_OPTS="-O2 -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -ffast-math -fsingle-precision-constant"
export WEBOS_CFLAGS="$WEBOS_OPTS -D__webos__ -D_GNU_SOURCE=1 -D_REENTRANT -I$PDK/include -I$PDK/include/SDL"
export WEBOS_LDFLAGS="-L$PDK/device/lib"

# Default test-device address; override per machine
export DEVICE_IP="${DEVICE_IP:-192.168.10.67}"
