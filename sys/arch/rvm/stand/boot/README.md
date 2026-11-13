# RVM FHR startup loader

Adapted from Rene W. Olsen's `rvm_SnakeOS/bootloader` and `includes_i`
checkout, with permission from the author in the porting session.
Original copyright notices are retained. This is bootstrap code for trusted
locally built images, not NetBSD user executable loading.

Local changes:

- The startup banner identifies the NetBSD/RVM loader.
- CR and INTE are cleared on entry, and caches stay disabled during loading.
  Clearing CR does not flush dirty cache lines in VMEmu, so the loader must
  not enable caches before handing control to the kernel startup.
- A shared `machine/bootinfo.h` describes the version 1 boot record. C
  preprocessing supplies offsets to the assembler so C and loader layouts
  agree. `r2` contains its address at kernel entry.
- The record reports one contiguous RAM bank starting at zero, loaded section
  addresses/sizes/permissions, the page-rounded loaded end and a reserved final
  RAM page for the loader stack. It is eight-byte aligned inside the raw loader.
- Section count, allocation size and stack bounds are checked before loading
  sections. The BSS clearing call now passes its length in the documented `r1`.
- CPU.i retains only CPU constants. SnakeOS-specific CPU structures and
  virtual-address policy are omitted; include paths are local to this copy.

VMAsm assembles Source.vma with `--bin --optimize`. VMEmu loads that raw
binary immediately followed by `netbsd.fhr` in one RAM region starting at
zero. Run one CPU for this initial port. The loader retains SnakeOS's RAM
probe, FHR parsing/relocation logic and initial fault handlers. NetBSD entry
replaces the stack and VBR before calling C; it never returns to this loader.

The assembly bootloader accepts an unpacked kernel. `./run.sh` and
`./run-kernel.sh` expand a zlib-packed `netbsd.fhr` into a temporary directory
before starting VMEmu. Direct `VMEmu -c vm.config` still requires an unpacked
kernel file.

Guest `halt` and `poweroff` now use VMEmu's privileged `emuexit` instruction
after NetBSD runs its shutdown hooks. Guest `reboot` uses privileged emulator
command 16, which reloads the VM configuration and boots again from the same
disk image. Fatal kernel stops still halt for inspection.
Use `/sbin/poweroff` or `/sbin/shutdown -p now` to exit VMEmu.
`/sbin/shutdown now` enters single-user mode, following NetBSD's normal
shutdown behavior. The minimal image has no rc.d services, so its
`/etc/rc.shutdown` is a no-op.

The version 1 handoff supports one contiguous physical RAM bank at zero;
it does not describe discontiguous memory or firmware reservations. Kernel
startup copies and validates it before enabling the MMU. Bootstrap reservations
are maintained by the early allocator; UVM registration is still pending.

## Linux TAP networking

VMEmu's optional `[NETWORK]` plugin exchanges Ethernet frames with a Linux
TAP interface. NetBSD/RVM attaches it as `rvmnet0` and polls for received
frames. The MMIO protocol uses aligned, big-endian 64-bit registers at
`0x0700000000004000`, with one physically addressed DMA buffer. The plugin
does not change the Ubuntu host's network settings.

On Ubuntu, create a TAP interface owned by the user who runs VMEmu. The
helper also enables IPv4 forwarding and adds NAT rules for the guest subnet:

```sh
sudo RVM_TAP_USER="$(id -un)" ./setup-rvm-network.sh
```

Run the helper again after an Ubuntu reboot. It is safe to rerun: it checks
the bridge, TAP, and firewall rules before adding them. `RVM_UPLINK` can
override the interface chosen from Ubuntu's default IPv4 route.

Build the image with a static guest address, then start it with the newly
built Linux VMEmu:

```sh
RVM_TAP=tap-rvm RVM_GUEST_IPV4=192.168.76.2/24 \
RVM_GUEST_DNS=1.1.1.1 \
./build-native-root.sh obj.rvm-native-regcall
VMEMU="$PWD/../rvm_VMEmu/bin/VMEmu_linux_SDL2" \
RVM_NATIVE_OBJDIR=obj.rvm-native-regcall ./run.sh
```

Without an override, `run.sh` chooses the most recently packaged image. Set
`RVM_NATIVE_IMAGE_DIR` to boot a specific image directory, including the
`obj.rvm-native-regcall/accel-wall-login-fixed` image built for ping and IRC checks with
kernel and user instruction caches plus the MMU TLB enabled.
`build-native-root.sh` now selects `DISKROOT_ACCEL` by default. Set
`RVM_KERNEL_CONFIG=DISKROOT_ICACHE` for kernel caching only, or
`RVM_KERNEL_CONFIG=DISKROOT_GENERIC` to disable both caches and the TLB.
`RVM_NATIVE_OBJDIR` still selects that object's `native-root` directory.
RVM login enables group write on the authenticated tty, allowing `wall` and
`shutdown` broadcasts to reach the console. The guest login profile also
enables `mesg y` for interactive shells.

Run VMEmu as the regular user named by `RVM_TAP_USER`; only the host setup
helper needs `sudo`. Using `sudo` for VMEmu can hide the user's audio session,
making SDL fail while opening ALSA. On a machine without audio, prefix the
launch command with `SDL_AUDIODRIVER=dummy`.

The guest gets `192.168.76.2/24`, default gateway `192.168.76.1`, and a DNS
resolver. The host can ping `192.168.76.2` once the guest boots. If you want to log
in and run `ping` from NetBSD, supply a root password file with
`RVM_MASTER_PASSWD=/path/to/master.passwd` during the build. The default
root account is locked. With an interactive login, the guest can also be
configured manually:

```sh
ifconfig rvmnet0 inet 192.168.76.2 netmask 255.255.255.0 up
ping -c 3 192.168.76.1
```

For a quick IRC TCP test, use `irc SERVER [PORT [NICK [#CHANNEL [KEY]]]]` in the
guest. It prints server lines, answers IRC `PING`, and joins the channel after
registration. Type a message to send it to the current channel. `/join #room`,
`/join #room key`, `/msg nick text`, `/nick newnick`, `/raw COMMAND`, and `/quit`
are supported. A channel key is sent only with `JOIN`, never with messages.
This small client uses plain TCP; use a trusted test server for this check.

The guest reaches other networks through Ubuntu's NAT. The TAP plugin needs
`/dev/net/tun` and permission to attach to the named interface.
The Linux kernel's [TUN/TAP documentation](https://docs.kernel.org/networking/tuntap.html)
describes the device and permission model.

The native root also includes `arp`, `traceroute`, `sockstat`, `ftp`, `telnet`,
`inetd`, `dhcpcd`, and `tcpdump`. The kernel exposes `/dev/bpf` for DHCP and
packet capture. The image above uses its static address, so `dhcpcd` should
only be started when the TAP's connected network provides DHCP. For RVM's
static build, `ftp` now links with OpenSSL and exposes TLS support; `telnet`
has no encryption or authentication extensions. The image includes OpenSSL,
SSH, `sshd`, and `ssh-keygen`. The build generates a unique Ed25519 host key
under the object directory and copies the build host's CA bundle when present.
OpenSSL 3.5 currently faults during guest crypto initialization, so TLS
handshakes and SSH sessions are not yet verified. `sshd` is not started on
boot while this is unresolved. `tcpdump -i rvmnet0` can inspect traffic inside
the guest after login.
