#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${RVM_NATIVE_OBJDIR:-}" ]; then
    obj=$RVM_NATIVE_OBJDIR
else
    obj="$script_dir/obj.rvm-native-root"
    # Prefer the standard build, then the latest validated optimization build.
    # Keep earlier images selectable through RVM_NATIVE_OBJDIR.
    for candidate in "$obj" \
        "$script_dir/obj.rvm-native-bit-ops" \
        "$script_dir/obj.rvm-native-gcc-opt" \
        "$script_dir/obj.rvm-native-clean"; do
        if [ -f "$candidate/native-root/vm.config" ]; then
            obj=$candidate
            break
        fi
    done
fi
case "$obj" in /*) ;; *) obj="$script_dir/$obj" ;; esac
image="$obj/native-root"

for file in bootloader.bin netbsd.fhr root-native.img vm.config; do
    if [ ! -f "$image/$file" ]; then
        echo "Missing $image/$file; run ./build-native-root.sh \"$obj\" first." >&2
        exit 1
    fi
done

# Use the generated configuration, including its HARDFILE attachment.
cd "$image"
echo "RVM native kernel: $image/netbsd.fhr"
exec "${VMEMU:-VMEmu}" -c vm.config "$@"
