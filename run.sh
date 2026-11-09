#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${RVM_NATIVE_OBJDIR:-}" ]; then
    obj=$RVM_NATIVE_OBJDIR
else
    obj="$script_dir/obj.rvm-native-root"
    # The current bring-up build lives here; prefer the standard build
    # directory once build-native-root.sh has generated an image there.
    if [ ! -f "$obj/native-root/vm.config" ] &&
       [ -f "$script_dir/obj.rvm-native-clean/native-root/vm.config" ]; then
        obj="$script_dir/obj.rvm-native-clean"
    fi
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
