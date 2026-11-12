# FHR shared libraries on NetBSD/RVM

The native libc now supplies `dlopen`, `dlsym`, `dlclose`, and `dlerror` for
FHR type-2 `.so` files. A library is made from relocatable FHR objects:

```sh
../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-gcc -O2 -c first.c -o first.o
../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-gcc -O2 -c second.c -o second.o
python3 sys/arch/rvm/stand/fhr_so.py \
    --ld ../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-ld \
    -o libexample.so first.o second.o
../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-nm libexample.so
```

Use `--needed` for libraries that import another library:

```sh
python3 sys/arch/rvm/stand/fhr_so.py \
    --ld ../rvm_gcc/rvm_cross_netbsd/bin/rvm-netbsd-ld \
    --needed libprovider.so -o libconsumer.so consumer.o
```

This uses the `LIB0` record already defined in SnakeOS `FHR.h`: a four-byte
ID, an eight-byte big-endian payload length for FHR64, then a FHR integer
count and pairs of `SYM0` name index and flags. Flags are zero for now. The
loader resolves each filename in the importing library's directory, loads it
before relocation, and retains it until the importer closes. Missing or cyclic
dependencies make `dlopen` fail.

Add `--zlib` to `fhr_so.py` to store the `.so` in the packed FHR format.
`dlopen` expands it before parsing sections. To pack any existing FHR file,
run `python3 sys/arch/rvm/stand/fhr_pack.py -o packed.fhr input.fhr`.
Use `-d` to restore the ordinary file. The packed wrapper stores two 32-bit
sizes for FHR32 or two 64-bit sizes for FHR64, always in big endian order.

Install the `.so` on the guest filesystem. A native program can open it by
path with `dlopen("/usr/lib/libexample.so", RTLD_NOW)` and use `dlsym` to find
its exported functions or data. The guest loader maps each section, applies
local ABS64 relocations, resolves imports against declared dependencies and
libraries loaded earlier with `RTLD_GLOBAL`, and protects code and read-only
data after relocation. GCC constructors in `.ctors` or `.init_array` run
after the library and its dependencies are ready. Destructors in `.dtors` or
`.fini_array` run on the final `dlclose` or at process exit. `dlsym` on a handle searches its dependencies;
`RTLD_DEFAULT` searches global libraries. `dlclose`
refuses to unload a library still used by another loaded library.

The main executable and libc are still statically linked. The loader does not
yet export symbols from the main executable, support TLS or versioned symbols,
or share read-only code pages between
processes. GCC's `-fPIC` is not yet a true GOT-based PIC implementation, so
code references to data can require private text relocations.

To rebuild the full image with zlib level 9 applied to every installed FHR
program, extra FHR library, and the kernel:

```sh
RVM_PACK_ALL_FHR=1 RVM_IMAGE_SIZE=48m RVM_SWAP_MIB=8 \
RVM_NATIVE_OUTPUT_DIR=obj.rvm-native-regcall/packed-max-root \
./build-native-root.sh obj.rvm-native-regcall
xz -9e -T1 -k -f obj.rvm-native-regcall/packed-max-root/root-native.img
```

The packer leaves the build outputs unpacked and packs files as it copies
them into the image. The bootloader requires an unpacked kernel;
`./run.sh` expands a packed kernel into a temporary boot directory.
The optional `.img.xz` file is a distribution archive; extract it before
giving the disk image to VMEmu.
The installed programs remain statically linked. The current FHR executable
linker cannot emit imports for ordinary programs, so placing libc and the
other system archives in shared libraries would not yet reduce their size.

Run `python3 sys/arch/rvm/tests/fhr_so_guest.py` after building the native
userland and kernel to make a disposable guest image and test calls across a
two-object `.so`, automatic dependencies, constructors and destructors,
including a large BSS section. This test does not change the image used by
`./run.sh`.
