#!/bin/bash
# build-diag.sh -- build the on-device diagnostic tools from webos/diag/.
# These are standalone probes, not part of the game: build them first on any
# new port to prove the toolchain + SDL + GLES stack before touching the engine.
set -e
cd "$(dirname "$0")/.."
source webos/env.sh
CC="$PDK_CC"   # diag tools stay on the PDK's own gcc 4.3.3 (proven combo)

OUT=stage/diag
mkdir -p "$OUT"

echo "== glsmoke (SDL + GLES1.1 smoke test, 1024x768 triangle + caps report)"
$CC $WEBOS_CFLAGS -o "$OUT/glsmoke" webos/diag/glsmoke.c \
    $WEBOS_LDFLAGS -lSDL -lpdl -lGLES_CM -lm

echo "== evread (evdev probe: names, caps, axis ranges, live events)"
$CC $WEBOS_CFLAGS -o "$OUT/evread" webos/diag/evread.c

echo "== egldiag (raw EGL interrogation via dlopen -- diagnostic only)"
$CC $WEBOS_CFLAGS -o "$OUT/egldiag" webos/diag/egldiag.c -ldl

echo "== eglspy.so / sdlspy.so (passive LD_PRELOAD loggers)"
$CC $WEBOS_CFLAGS -shared -fPIC -o "$OUT/eglspy.so" webos/diag/eglspy.c -ldl
$CC $WEBOS_CFLAGS -shared -fPIC -o "$OUT/sdlspy.so" webos/diag/sdlspy.c -ldl

echo "== netprobe (jailed DNS/HTTP probe)"
$CC $WEBOS_CFLAGS -o "$OUT/netprobe" webos/diag/netprobe.c

scripts/check-symbols.sh "$OUT"/glsmoke "$OUT"/evread "$OUT"/egldiag "$OUT"/netprobe
echo "diag tools in $OUT"
