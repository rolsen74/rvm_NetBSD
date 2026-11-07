#!/bin/sh
# Bootstrap NetBSD host tools and build the normal MI RVM kernel.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$root"
config=${1:-EARLY}
case "$config" in *[!A-Za-z0-9_.-]*|'') echo "Invalid kernel configuration" >&2; exit 1;; esac
obj=${RVM_KERNEL_OBJDIR:-"$root/obj.rvm-kernel"}
mkdir -p "$obj"
obj=$(CDPATH= cd -- "$obj" && pwd)
tools=${RVM_KERNEL_TOOLDIR:-"$obj/hosttools"}
toolchain=${RVM_TOOLCHAIN:-"$root/../rvm_gcc/rvm_cross_netbsd"}
toolchain=$(CDPATH= cd -- "$toolchain" && pwd)
jobs=${RVM_JOBS:-4}

# Fail before a large build if the external compiler lacks the required hooks.
probe=$(mktemp -d "$obj/compiler.XXXXXX")
trap 'rm -rf "$probe"' EXIT HUP INT TERM
cat > "$probe/probe.c" <<'EOF'
#if !defined(__RVM_CALLER_ARG_HOME__) || __RVM_CALLER_ARG_HOME__ != 64
#error Rebuild GCC with the RVM caller-home ABI correction
#endif
extern int target;
const void *pointer = &target;
char aligned[65536] __attribute__((aligned(65536)));
EOF
"$toolchain/bin/rvm-netbsd-gcc" -ffreestanding -fno-pic -fno-pie \
    -S "$probe/probe.c" -o "$probe/probe.s"
python3 - "$probe/probe.s" <<'PY'
from pathlib import Path
import re, sys
s = Path(sys.argv[1]).read_text()
if not re.search(r'\.balign\s+65536', s) or not re.search(r'\.quad\s+target', s):
    sys.exit('Rebuild GCC with sys/arch/rvm/stand/toolchain/rvm-data-alignment.patch')
PY

./build.sh -m rvm -U -u -O "$obj" -T "$tools" \
    -V EXTERNAL_TOOLCHAIN="$toolchain" -V MKGDB=no makewrapper
subset='host-mkdep compat binstall .WAIT date mktemp sed m4 yacc .WAIT lex .WAIT awk genassym lorder tsort mkdep cksum stat config'
"$tools/bin/nbmake-rvm" -C tools SUBDIR="$subset" obj
"$tools/bin/nbmake-rvm" -C tools SUBDIR="$subset" -j "$jobs" build_install
./build.sh -m rvm -U -u -j "$jobs" -O "$obj" -T "$tools" \
    -V EXTERNAL_TOOLCHAIN="$toolchain" -V MKGDB=no "kernel=$config"

make -f sys/arch/rvm/Makefile.bootstrap OBJDIR="$obj/loader" \
    CROSS_COMPILE="$toolchain/bin/rvm-netbsd-" "$obj/loader/bootloader.bin"
cp "$obj/loader/bootloader.bin" "$obj/bootloader.bin"
cp "$obj/sys/arch/rvm/compile/$config/netbsd" "$obj/netbsd.fhr.new"
mv "$obj/netbsd.fhr.new" "$obj/netbsd.fhr"
cp sys/arch/rvm/conf/vmemu.conf "$obj/vm.config"
echo "RVM $config image: $obj/netbsd.fhr"
echo "Run ./run-kernel.sh (use the same RVM_KERNEL_OBJDIR if overridden)."
