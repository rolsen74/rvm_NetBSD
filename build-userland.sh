#!/bin/sh
# Build native static libc/csu through the ordinary NetBSD makefiles.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$root/sys/arch/rvm/stand/build_userland.py" "$@"
