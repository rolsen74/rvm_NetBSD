#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "${VMEMU:-}" ] && [ -x "$script_dir/../rvm_VMEmu/bin/VMEmu_linux_SDL2" ]; then
    VMEMU="$script_dir/../rvm_VMEmu/bin/VMEmu_linux_SDL2"
    export VMEMU
fi
if [ -n "${RVM_NATIVE_IMAGE_DIR:-}" ]; then
    image=$RVM_NATIVE_IMAGE_DIR
    case "$image" in /*) ;; *) image="$script_dir/$image" ;; esac
elif [ -n "${RVM_NATIVE_OBJDIR:-}" ]; then
    obj=$RVM_NATIVE_OBJDIR
    case "$obj" in /*) ;; *) obj="$script_dir/$obj" ;; esac
    image="$obj/native-root"
else
    image=
    # Choose the most recently packaged image. Guest disk writes do not
    # change vm.config, so rebooting an older image cannot change this choice.
    for candidate in \
        "$script_dir/obj.rvm-native-root/native-root" \
        "$script_dir/obj.rvm-native-regcall/native-root" \
        "$script_dir/obj.rvm-native-regcall/password-shared-root" \
        "$script_dir/obj.rvm-native-regcall/fivecpu-root" \
        "$script_dir/obj.rvm-native-regcall/fivecpu-icnt-root" \
        "$script_dir/obj.rvm-native-regcall/irc-test-root" \
        "$script_dir/obj.rvm-native-regcall/irc-login-fixed" \
        "$script_dir/obj.rvm-native-regcall/irc-unpacked-root" \
        "$script_dir/obj.rvm-native-regcall/icache-root" \
        "$script_dir/obj.rvm-native-regcall/accel-root" \
        "$script_dir/obj.rvm-native-regcall/accel-irc-fixed" \
        "$script_dir/obj.rvm-native-regcall/accel-shutdown-fixed" \
        "$script_dir/obj.rvm-native-regcall/accel-broadcast-fixed" \
        "$script_dir/obj.rvm-native-regcall/accel-wall-login-fixed" \
        "$script_dir/obj.rvm-native-bit-ops/native-root" \
        "$script_dir/obj.rvm-native-gcc-opt/native-root" \
        "$script_dir/obj.rvm-native-clean/native-root"; do
        if [ -f "$candidate/vm.config" ] && \
            { [ -z "$image" ] || [ "$candidate/vm.config" -nt "$image/vm.config" ]; }; then
            image=$candidate
        fi
    done
    if [ -z "$image" ]; then
        echo "No native image found; run ./build-native-root.sh first." >&2
        exit 1
    fi
fi

for file in bootloader.bin netbsd.fhr root-native.img vm.config; do
    if [ ! -f "$image/$file" ]; then
        echo "Missing $image/$file; rebuild or select a complete image." >&2
        exit 1
    fi
done

# A configured TAP is optional. Probe the same TUNSETIFF operation VMEmu
# uses, since the device node may exist without permission to attach it.
offline=${RVM_NO_NETWORK:-0}
if [ "$offline" != 1 ] && grep -q '^\[NETWORK\]' "$image/vm.config"; then
    if ! python3 - "$image/vm.config" <<'PY'
import fcntl
import os
import re
import struct
import sys

config = open(sys.argv[1], encoding='utf-8').read()
network = re.search(r'(?ms)^\[NETWORK\]\s*\n(.*?)^\s*End\s*$', config)
tap = re.search(r'(?m)^\s*Tap\s*=\s*"([A-Za-z0-9_.-]{1,15})"\s*$',
                network.group(1)) if network else None
if not tap:
    sys.exit(1)
try:
    fd = os.open('/dev/net/tun', os.O_RDWR | os.O_NONBLOCK)
    try:
        request = struct.pack('16sH22x', tap.group(1).encode(), 0x1002)
        fcntl.ioctl(fd, 0x400454ca, request)  # TUNSETIFF, IFF_TAP|IFF_NO_PI
    finally:
        os.close(fd)
except OSError:
    sys.exit(1)
PY
    then
        offline=1
    fi
fi

echo "RVM native kernel: $image/netbsd.fhr"
echo "RVM emulator: ${VMEMU:-$(command -v VMEmu || printf 'VMEmu')}"
if [ "$offline" = 1 ] && grep -q '^\[NETWORK\]' "$image/vm.config"; then
    if [ "${RVM_REQUIRE_TAP:-0}" = 1 ]; then
        echo "TAP attachment unavailable (RVM_REQUIRE_TAP=1)." >&2
        exit 1
    fi
    echo "TAP attachment unavailable; starting without guest networking." >&2
    offline_image=$(mktemp -d "${TMPDIR:-/tmp}/rvm-offline.XXXXXX")
    trap 'rm -rf "$offline_image"' EXIT HUP INT TERM
    for source in "$image"/*; do
        ln -s "$source" "$offline_image/$(basename "$source")"
    done
    rm "$offline_image/vm.config"
    awk '
        /^\[NETWORK\][[:space:]]*$/ { skip = 1; next }
        skip && /^[[:space:]]*End[[:space:]]*$/ { skip = 0; next }
        !skip { print }
    ' "$image/vm.config" > "$offline_image/vm.config"
    python3 "$script_dir/sys/arch/rvm/stand/fhr_run.py" "$offline_image" "$@"
else
    exec python3 "$script_dir/sys/arch/rvm/stand/fhr_run.py" "$image" "$@"
fi
