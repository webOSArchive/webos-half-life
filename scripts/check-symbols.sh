#!/bin/bash
# check-symbols.sh -- gate every shipped ELF on GLIBC symbol versions.
#
# The TouchPad has glibc 2.8. Any ELF that needs a newer GLIBC_* version will
# fail to load on device with "version 'GLIBC_X.YY' not found" -- or worse,
# silently for a dlopen()ed module. Run this on EVERY binary and .so that goes
# into the ipk. Exits nonzero on the first violation.
#
# Usage: check-symbols.sh <elf> [<elf> ...]
set -u
PDK=/opt/PalmPDK
READELF="$PDK/arm-gcc/bin/arm-none-linux-gnueabi-readelf"
[ -x "$READELF" ] || READELF=readelf

MAX_MAJOR=2
MAX_MINOR=8
fail=0

for f in "$@"; do
    if [ ! -f "$f" ]; then
        echo "check-symbols: MISSING $f" >&2
        fail=1
        continue
    fi
    vers=$("$READELF" -V "$f" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV)
    bad=""
    for v in $vers; do
        major=${v#GLIBC_}; minor=${major#*.}; major=${major%%.*}
        minor=${minor%%.*}   # GLIBC_2.3.4 -> 3
        if [ "$major" -gt $MAX_MAJOR ] || { [ "$major" -eq $MAX_MAJOR ] && [ "$minor" -gt $MAX_MINOR ]; }; then
            bad="$bad $v"
        fi
    done
    if [ -n "$bad" ]; then
        echo "check-symbols: FAIL $f needs$bad (device max GLIBC_${MAX_MAJOR}.${MAX_MINOR})" >&2
        fail=1
    else
        echo "check-symbols: ok   $f (${vers:-no GLIBC refs})"
    fi
done
exit $fail
