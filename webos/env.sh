# webos/env.sh -- cross-compilation environment for the webOS TouchPad GoldSrc port.
# Source this from every build script:  source "$(dirname "$0")/../webos/env.sh"
#
# TOOLCHAIN CHOICE (Decision D1 -- see docs/NOTES.md):
#   Default is the PDK's own CodeSourcery gcc 4.3.3. It targets the PDK's
#   glibc-2.5 sysroot; the device has glibc 2.8, so its output always loads.
#   Do NOT switch to Linaro 4.9.4 without --sysroot=$PDK/arm-gcc/sysroot --
#   Linaro's own sysroot links libm against GLIBC_2.15 and the binary will not
#   load on device (sdlquake learned this the hard way).
#
# FLAG RULES (all learned on hardware, do not "clean up"):
#   - NO -funroll-loops : internal compiler error in gcc 4.3.3
#   - NO -fsigned-char  : 1990s engines have char-indexed tables; ARM's default
#                         unsigned char is what they shipped with
#   - softfp, not hard  : the whole PDK ABI is softfp

PDK=/opt/PalmPDK
TOOLCHAIN_BIN="$PDK/arm-gcc/bin"
CROSS_PREFIX="$TOOLCHAIN_BIN/arm-none-linux-gnueabi-"

export CC="${CROSS_PREFIX}gcc"
export CXX="${CROSS_PREFIX}g++"
export AR="${CROSS_PREFIX}ar"
export RANLIB="${CROSS_PREFIX}ranlib"
export STRIP="${CROSS_PREFIX}strip"
export READELF="${CROSS_PREFIX}readelf"

# Optimization + ABI. -ffast-math/-fsingle-precision-constant matter on a
# Cortex-A8 whose double-precision VFP is not pipelined.
export WEBOS_OPTS="-O2 -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -ffast-math -fsingle-precision-constant"
export WEBOS_CFLAGS="$WEBOS_OPTS -D__webos__ -D_GNU_SOURCE=1 -D_REENTRANT -I$PDK/include -I$PDK/include/SDL"
export WEBOS_LDFLAGS="-L$PDK/device/lib"

# Device: see /home/jonwise/Projects/webos-hardware-tests/DEVICE-STATE.md
export DEVICE_IP=192.168.10.67
