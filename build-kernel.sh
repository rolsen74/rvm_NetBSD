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

# make cannot otherwise see an external GCC rebuild. Keep a content stamp so
# both kernel and libkern objects are rebuilt when installed tools change.
RVM_TOOLCHAIN_STAMP="$obj/toolchain.stamp"
export RVM_TOOLCHAIN_STAMP
python3 - "$toolchain/bin/rvm-netbsd-gcc" "$RVM_TOOLCHAIN_STAMP" <<'PY'
import hashlib, os, shutil, subprocess, sys
from pathlib import Path
cc, stamp = sys.argv[1], Path(sys.argv[2])
paths = [Path(cc)]
for name in ('cc1', 'as', 'ld'):
    program = subprocess.check_output([cc, '-print-prog-name='+name], text=True).strip()
    path = Path(program) if '/' in program else Path(shutil.which(program) or program)
    paths.append(path)
paths.append(Path(shutil.which(os.environ.get('VMASM', 'VMAsm')) or 'VMAsm'))
digest = hashlib.sha256()
for path in paths:
    digest.update(str(path.resolve()).encode())
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            digest.update(block)
value = digest.hexdigest()+'\n'
if not stamp.exists() or stamp.read_text() != value:
    stamp.write_text(value)
    print('RVM toolchain changed: rebuilding kernel and libkern objects')
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
