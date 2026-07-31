#!/bin/bash
# deploy.sh -- push a file to the device and optionally run it.
#
#   deploy.sh put <local> <device-path>      copy a file over novacom
#   deploy.sh run <device-path> [args...]    run a binary on device
#   deploy.sh push-run <local> [args...]     copy to /media/internal and run
#   deploy.sh log [<name>]                   cat /media/internal/<name>.log (default xash)
#   deploy.sh shell                          note: use 'novaterm' interactively
#
# novacom quirks (learned in sdlquake): `novacom run 'cmd'` word-splits and
# quoted commands return "unknown command" -- for anything non-trivial, write a
# script locally, push it, and run it with /bin/sh.
set -e
cd "$(dirname "$0")/.."
source webos/env.sh

case "${1:-}" in
put)
    novacom put "file://$3" < "$2"
    echo "put $2 -> $3"
    ;;
run)
    shift
    p="$1"; shift
    novacom run "file://$p" -- "$@"
    ;;
push-run)
    shift
    local_file="$1"; shift
    base=$(basename "$local_file")
    novacom put "file:///media/internal/$base" < "$local_file"
    # novacom run word-splits its command argument (and quoted commands return
    # "unknown command"), so: write a runner script locally, push it, sh it.
    # /media/internal is noexec -- copy to /tmp (tmpfs, exec) and run there.
    tmpscript=$(mktemp)
    cat > "$tmpscript" <<EOF
cp /media/internal/$base /tmp/$base
chmod 755 /tmp/$base
/tmp/$base $*
EOF
    novacom put file:///tmp/webos-run.sh < "$tmpscript"
    rm -f "$tmpscript"
    novacom run file:///bin/sh -- /tmp/webos-run.sh
    ;;
log)
    novacom run file:///bin/cat -- "/media/internal/${2:-xash}.log"
    ;;
*)
    grep '^#' "$0" | head -12
    exit 1
    ;;
esac
