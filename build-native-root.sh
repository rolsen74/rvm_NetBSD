#!/bin/sh
# Build a native static login image using the production disk-root kernel.
set -eu
case "${1:-}" in
    -h|--help)
        echo 'Usage: ./build-native-root.sh [object-directory]'
        echo 'Optional environment: RVM_TOOLCHAIN, RVM_JOBS, RVM_KERNEL_CONFIG, RVM_MASTER_PASSWD, RVM_NATIVE_OUTPUT_DIR, RVM_PACK_ALL_FHR, RVM_IMAGE_SIZE, RVM_SWAP_MIB, RVM_TAP, RVM_GUEST_IPV4, RVM_GUEST_GATEWAY, RVM_GUEST_DNS, RVM_CA_BUNDLE'
        exit 0;;
esac
if [ "$#" -gt 1 ]; then
    echo 'Expected at most one object-directory argument' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$root"
obj=${1:-${RVM_NATIVE_OBJDIR:-"$root/obj.rvm-native-root"}}
cross=${RVM_TOOLCHAIN:-"$root/../rvm_gcc/rvm_cross_netbsd"}
mkdir -p "$obj"
obj=$(CDPATH= cd -- "$obj" && pwd)
./build-userland.sh --objdir "$obj" --toolchain "$cross" -j "${RVM_JOBS:-4}" \
    --libm \
    --library lib/libutil \
    --library lib/librt \
    --library lib/libterminfo \
    --library lib/libcurses \
    --library lib/libedit \
    --library lib/libkvm \
    --library lib/libcrypt \
    --library lib/libprop \
    --library crypto/external/bsd/netpgp/libmj \
    --library lib/libz \
    --library lib/libbz2 \
    --library external/bsd/file/lib \
    --library lib/librmt \
    --library lib/liby \
    --library lib/libintl \
    --library lib/libresolv \
    --library lib/libpci \
    --library lib/libbsdmalloc \
    --library lib/libposix \
    --library external/bsd/liblzf/lib \
    --library lib/libpanel \
    --library lib/libmenu \
    --library lib/libform \
    --library lib/libcompat \
    --library lib/librpcsvc \
    --library lib/libossaudio \
    --library external/bsd/tre/lib \
    --library external/bsd/libnv/lib \
    --library lib/libnpf \
    --library lib/libipsec \
    --library lib/libquota \
    --library lib/libusbhid \
    --library lib/libdm \
    --library external/bsd/flex/lib \
    --library external/bsd/blocklist/lib \
    --library lib/libwrap \
    --library crypto/external/bsd/openssl/lib/libdes \
    --library lib/libtelnet \
    --library external/bsd/libpcap/lib \
    --library external/bsd/libevent/lib/libevent \
    --library crypto/external/apache2/openssl/lib/libcrypto \
    --library crypto/external/apache2/openssl/lib/libssl \
    --library crypto/external/apache2/openssl/lib/libapps \
    --library crypto/external/bsd/openssh/lib \
    -V HAVE_OPENSSL=35 \
    -V USE_KERBEROS=no \
    -V USE_LDAP=no \
    -V USE_OPENSSL=no \
    -V USE_PAM=no \
    -V MKPAM=no \
    -V USE_SKEY=no \
    --program bin/sh \
    --program bin/csh \
    --program bin/echo \
    --program bin/cat \
    --program bin/pwd \
    --program bin/sync \
    --program bin/ls \
    --program bin/test \
    --program bin/mkdir \
    --program bin/cp \
    --program bin/mv \
    --program bin/rm \
    --program bin/stty \
    --program bin/sleep \
    --program bin/chmod \
    --program bin/ln \
    --program bin/kill \
    --program bin/hostname \
    --program bin/date \
    --program bin/df \
    --program bin/expr \
    --program bin/ed \
    --program bin/pax \
    --program bin/dd \
    --program bin/rmdir \
    --program bin/domainname \
    --program bin/ps \
    --program usr.bin/yes \
    --program usr.bin/gettext \
    --program usr.bin/locate/bigram \
    --program usr.bin/locate/code \
    --program usr.bin/locate/locate \
    --program usr.bin/mail \
    --program usr.bin/id \
    --program usr.bin/basename \
    --program usr.bin/dirname \
    --program usr.bin/env \
    --program usr.bin/head \
    --program usr.bin/tail \
    --program usr.bin/wc \
    --program usr.bin/printf \
    --program usr.bin/uname \
    --program usr.bin/touch \
    --program usr.bin/grep \
    --program usr.bin/sed \
    --program usr.bin/cut \
    --program usr.bin/tr \
    --program usr.bin/sort \
    --program usr.bin/uniq \
    --program usr.bin/xargs \
    --program usr.bin/find \
    --program usr.bin/du \
    --program usr.bin/cksum \
    --program usr.bin/cmp \
    --program usr.bin/tee \
    --program usr.bin/stat \
    --program usr.bin/jot \
    --program usr.bin/seq \
    --program usr.bin/comm \
    --program usr.bin/fold \
    --program usr.bin/join \
    --program usr.bin/paste \
    --program usr.bin/mktemp \
    --program usr.bin/realpath \
    --program usr.bin/hexdump \
    --program usr.bin/tsort \
    --program external/historical/nawk/bin \
    --program usr.bin/column \
    --program usr.bin/rev \
    --program usr.bin/split \
    --program usr.bin/nl \
    --program usr.bin/pr \
    --program usr.bin/cal \
    --program usr.bin/patch \
    --program usr.bin/diff \
    --program usr.bin/diff3 \
    --program usr.bin/m4 \
    --program usr.bin/time \
    --program usr.bin/tty \
    --program usr.bin/fmt \
    --program usr.bin/compress \
    --program usr.bin/make \
    --program usr.bin/nice \
    --program usr.bin/nohup \
    --program usr.bin/getconf \
    --program usr.bin/expand \
    --program usr.bin/unexpand \
    --program usr.bin/col \
    --program usr.bin/chflags \
    --program usr.bin/logger \
    --program usr.bin/mesg \
    --program usr.bin/whereis \
    --program usr.bin/look \
    --program usr.bin/banner \
    --program usr.bin/colrm \
    --program usr.bin/tput \
    --program usr.bin/renice \
    --program usr.bin/who \
    --program usr.bin/users \
    --program sbin/init \
    --program libexec/getty \
    --program usr.bin/login \
    --program usr.bin/passwd \
    --program usr.sbin/pwd_mkdb \
    --program sbin/chown \
    --program sbin/fsck \
    --program sbin/fsck_ffs \
    --program sbin/mount_tmpfs \
    --program sbin/mount_ptyfs \
    --program sbin/mount \
    --program sbin/mount_ffs \
    --program sbin/umount \
    --program sbin/reboot \
    --program sbin/dmesg \
    --program sbin/sysctl \
    --program usr.bin/w \
    --program usr.bin/pkill \
    --program usr.bin/vmstat \
    --program usr.bin/netstat \
    --program usr.bin/fstat \
    --program sbin/ifconfig \
    --program sbin/route \
    --program sbin/disklabel \
    --program sbin/swapctl \
    --program sbin/newfs \
    --program sbin/tunefs \
    --program sbin/ping \
    --program external/bsd/less/bin/less \
    --program usr.bin/vis \
    --program usr.bin/base64 \
    --program usr.bin/uuencode \
    --program usr.bin/uudecode \
    --program usr.bin/what \
    --program external/bsd/nvi/usr.bin/nvi \
    --program usr.bin/gzip \
    --program usr.bin/bzip2 \
    --program usr.bin/script \
    --program external/bsd/bc/bin \
    --program external/bsd/top/bin \
    --program external/bsd/file/bin \
    --program usr.bin/unvis \
    --program usr.bin/getopt \
    --program usr.bin/lam \
    --program usr.bin/rs \
    --program usr.bin/shuffle \
    --program usr.bin/timeout \
    --program usr.bin/mkfifo \
    --program usr.bin/pwait \
    --program usr.bin/printenv \
    --program usr.bin/progress \
    --program usr.bin/uuidgen \
    --program usr.bin/fpr \
    --program usr.bin/asa \
    --program usr.bin/csplit \
    --program usr.bin/checknr \
    --program usr.bin/flock \
    --program usr.bin/deroff \
    --program usr.bin/locale \
    --program usr.bin/fincore \
    --program usr.bin/getent \
    --program usr.bin/pathchk \
    --program usr.bin/unifdef \
    --program usr.bin/ctags \
    --program usr.bin/colcrt \
    --program usr.bin/logname \
    --program usr.bin/soelim \
    --program usr.bin/apply \
    --program usr.bin/sdiff \
    --program usr.bin/shlock \
    --program usr.bin/gencat \
    --program usr.bin/pwhash \
    --program usr.bin/bzip2recover \
    --program usr.bin/cap_mkdb \
    --program usr.bin/db \
    --program usr.bin/sortinfo \
    --program usr.bin/leave \
    --program usr.bin/fsplit \
    --program usr.bin/tabs \
    --program usr.bin/lastcomm \
    --program usr.bin/infocmp \
    --program usr.bin/tset \
    --program usr.bin/resize \
    --program usr.bin/units \
    --program usr.bin/qsubst \
    --program usr.bin/from \
    --program usr.bin/whois \
    --program usr.bin/wall \
    --program usr.bin/getaddrinfo \
    --program usr.bin/getnameinfo \
    --program usr.bin/innetgr \
    --program usr.bin/revoke \
    --program usr.bin/error \
    --program usr.bin/nbperf \
    --program usr.bin/tcopy \
    --program usr.bin/vndcompress \
    --program usr.bin/atto \
    --program usr.bin/last \
    --program usr.bin/lock \
    --program usr.bin/fgen \
    --program usr.bin/mkcsmapper \
    --program usr.bin/mkesdb \
    --program usr.bin/mklocale \
    --program usr.bin/ipcs \
    --program usr.bin/ipcrm \
    --program usr.bin/mkubootimage \
    --program usr.bin/mkdep \
    --program usr.bin/rpcinfo \
    --program usr.bin/netgroup \
    --program usr.bin/ul \
    --program usr.bin/write \
    --program usr.bin/tic \
    --program usr.bin/calendar \
    --program usr.bin/indent \
    --program usr.bin/tftp \
    --program usr.bin/finger \
    --program usr.bin/dc \
    --program usr.bin/iconv \
    --program sbin/mknod \
    --program sbin/rcorder \
    --program sbin/rndctl \
    --program sbin/scan_ffs \
    --program sbin/mount_kernfs \
    --program sbin/mount_procfs \
    --program sbin/mount_union \
    --program sbin/mount_msdos \
    --program sbin/fsck_msdos \
    --program sbin/mount_cd9660 \
    --program sbin/mount_ext2fs \
    --program sbin/fsck_ext2fs \
    --program usr.sbin/chroot \
    --program usr.sbin/rdate \
    --program usr.sbin/zic \
    --program usr.sbin/lastlogin \
    --program usr.sbin/accton \
    --program usr.sbin/quot \
    --program usr.sbin/mtree \
    --program usr.sbin/services_mkdb \
    --program usr.bin/newsyslog \
    --program usr.bin/ktrace \
    --program usr.bin/rlogin \
    --program sbin/newfs_ext2fs \
    --program sbin/fsirand \
    --program sbin/blkdiscard \
    --program sbin/mount_fdesc \
    --program sbin/mount_overlay \
    --program sbin/mount_ntfs \
    --program sbin/mount_lfs \
    --program sbin/fsck_lfs \
    --program sbin/dkctl \
    --program sbin/drvctl \
    --program usr.sbin/dumpfs \
    --program usr.sbin/zdump \
    --program usr.sbin/unlink \
    --program usr.sbin/diskpart \
    --program usr.sbin/cpuctl \
    --program usr.sbin/quotaon \
    --program usr.sbin/repquota \
    --program usr.bin/rsh \
    --program usr.bin/showmount \
    --program usr.bin/quota \
    --program usr.bin/nc \
    --program usr.bin/rpcgen \
    --program usr.bin/gcore \
    --program usr.bin/apropos \
    --program usr.bin/biff \
    --program usr.bin/extattr \
    --program usr.bin/ruptime \
    --program usr.bin/rwho \
    --program usr.bin/vacation \
    --program usr.bin/whatis \
    --program sbin/clri \
    --program sbin/shutdown \
    --program sbin/ttyflags \
    --program usr.sbin/ac \
    --program usr.sbin/catman \
    --program usr.sbin/mailwrapper \
    --program usr.sbin/paxctl \
    --program usr.sbin/tcpdrop \
    --program usr.sbin/wakeonlan \
    --program usr.bin/nfsstat \
    --program usr.sbin/iostat \
    --program bin/getfacl \
    --program bin/setfacl \
    --program usr.sbin/rmt \
    --program bin/ksh \
    --program usr.sbin/arp \
    --program usr.sbin/traceroute \
    --program usr.bin/sockstat \
    --program usr.bin/ftp \
    --program usr.bin/telnet \
    --program usr.bin/irc \
    --program usr.sbin/inetd \
    --program external/bsd/dhcpcd/sbin/dhcpcd \
    --program external/bsd/tcpdump/bin \
    --program crypto/external/apache2/openssl/bin \
    --program crypto/external/bsd/openssh/bin/ssh \
    --program crypto/external/bsd/openssh/bin/sshd \
    --program crypto/external/bsd/openssh/bin/ssh-keygen \
    --program crypto/external/bsd/openssh/libexec/sshd-session \
    --program crypto/external/bsd/openssh/libexec/sshd-auth \
    --program sbin/dump \
    --program sbin/fdisk \
    --program sbin/fsdb \
    --program sbin/gpt \
    --program sbin/mount_nfs \
    --program sbin/newfs_msdos \
    --program sbin/ping6 \
    --program sbin/resize_ffs \
    --program sbin/restore \
    --program usr.bin/chpass \
    --program usr.bin/newgrp \
    --program usr.bin/su \
    --program usr.bin/systat \
    --program usr.sbin/dev_mkdb \
    --program usr.sbin/fstyp \
    --program usr.sbin/ifmcstat \
    --program usr.sbin/ip6addrctl \
    --program usr.sbin/ndp \
    --program usr.sbin/rpcbind \
    --program usr.sbin/rtadvd \
    --program libexec/fingerd \
    --program libexec/ftpd \
    --program libexec/mail.local \
    --program libexec/makekey \
    --program libexec/rlogind \
    --program libexec/rexecd \
    --program libexec/rpc.rquotad \
    --program libexec/rpc.rstatd \
    --program libexec/rpc.rusersd \
    --program libexec/rshd \
    --program libexec/talkd \
    --program libexec/tftpd \
    --program libexec/utmp_update \
    --program sbin/fsck_udf \
    --program sbin/mount_null \
    --program sbin/mount_udf \
    --program sbin/mount_umap \
    --program usr.sbin/ifwatchd \
    --program usr.sbin/user \
    --program usr.sbin/vipw \
    --program libexec/comsat \
    --program libexec/identd \
    --program libexec/rpc.rwalld \
    --program libexec/rpc.sprayd \
    --program libexec/telnetd \
    --program sbin/brconfig \
    --program sbin/ccdconfig \
    --program sbin/dkscan_bsdlabel \
    --program sbin/mbrlabel \
    --program sbin/modstat \
    --program sbin/raidctl \
    --program usr.sbin/intrctl \
    --program usr.sbin/link \
    --program usr.sbin/mountd \
    --program usr.sbin/netgroup_mkdb \
    --program usr.sbin/route6d \
    --program usr.sbin/rpc.statd \
    --program usr.sbin/syslogd \
    --program usr.sbin/tcpdchk \
    --program usr.sbin/tcpdmatch \
    --program libexec/atrun \
    --program sbin/fsck_v7fs \
    --program sbin/modload \
    --program sbin/modunload \
    --program sbin/mount_v7fs \
    --program sbin/newfs_udf \
    --program sbin/newfs_v7fs \
    --program sbin/savecore \
    --program usr.bin/at \
    --program usr.bin/man \
    --program usr.bin/msgs \
    --program usr.sbin/edquota \
    --program usr.sbin/psrset \
    --program usr.sbin/quotacheck \
    --program usr.sbin/quotarestore \
    --program usr.sbin/schedctl \
    --program usr.sbin/wg-keygen \
    --program usr.sbin/wgconfig \
    --program external/bsd/cron/bin/cron \
    --program external/bsd/cron/bin/crontab \
    --program bin/rcp \
    --program sbin/mount_ados \
    --program sbin/mount_efs \
    --program sbin/mount_hfs \
    --program sbin/mount_nilfs \
    --program sbin/mount_sysvbfs \
    --program sbin/newfs_sysvbfs \
    --program usr.bin/rdist \
    --program usr.bin/rup \
    --program usr.bin/rusers \
    --program usr.bin/rwall \
    --program usr.sbin/envstat \
    --program usr.sbin/mld6query \
    --program usr.sbin/mrinfo \
    --program usr.sbin/mtrace \
    --program usr.sbin/rarpd \
    --program usr.sbin/rip6query \
    --program usr.sbin/rpc.bootparamd \
    --program usr.sbin/rpc.pcnfsd \
    --program usr.sbin/traceroute6
RVM_KERNEL_OBJDIR="$obj/kernel" RVM_KERNEL_TOOLDIR="$obj/hosttools" \
    RVM_TOOLCHAIN="$cross" ./build-kernel.sh "${RVM_KERNEL_CONFIG:-DISKROOT_ACCEL}"
python3 sys/arch/rvm/stand/build_fhr_demo.py --objdir "$obj" --toolchain "$cross"
set -- --objdir "$obj" --kernel-objdir "$obj/kernel" \
    --extra-file "usr/bin/fhr-so-demo=$obj/fhr-demo/fhr-so-demo" \
    --extra-file "usr/lib/librvmtest.so=$obj/fhr-demo/librvmtest.so" \
    --extra-file "usr/lib/librvmprovider.so=$obj/fhr-demo/librvmprovider.so" \
    --extra-file "usr/lib/librvmconsumer.so=$obj/fhr-demo/librvmconsumer.so" \
    --extra-file "usr/lib/librvmunresolved.so=$obj/fhr-demo/librvmunresolved.so" \
    --extra-file "usr/lib/librvmmissing.so=$obj/fhr-demo/librvmmissing.so" \
    --extra-file "usr/lib/librvmcyclea.so=$obj/fhr-demo/librvmcyclea.so" \
    --extra-file "usr/lib/librvmcycleb.so=$obj/fhr-demo/librvmcycleb.so" \
    --extra-file "usr/lib/librvmlifecycleprovider.so=$obj/fhr-demo/librvmlifecycleprovider.so" \
    --extra-file "usr/lib/librvmlifecycle.so=$obj/fhr-demo/librvmlifecycle.so"
ssh_key_dir="$obj/ssh-host-keys"
mkdir -p "$ssh_key_dir"
if [ ! -f "$ssh_key_dir/ssh_host_ed25519_key" ]; then
    ssh-keygen -q -t ed25519 -N '' -f "$ssh_key_dir/ssh_host_ed25519_key"
fi
set -- "$@" \
    --extra-file "etc/ssh/ssh_host_ed25519_key=$ssh_key_dir/ssh_host_ed25519_key" \
    --extra-file "etc/ssh/ssh_host_ed25519_key.pub=$ssh_key_dir/ssh_host_ed25519_key.pub"
ca_bundle=${RVM_CA_BUNDLE:-/etc/ssl/certs/ca-certificates.crt}
if [ -f "$ca_bundle" ]; then
    set -- "$@" --extra-file "etc/openssl/cert.pem=$ca_bundle"
fi
if [ -n "${RVM_NATIVE_OUTPUT_DIR:-}" ]; then
    set -- "$@" --output-dir "$RVM_NATIVE_OUTPUT_DIR"
fi
if [ -n "${RVM_MASTER_PASSWD:-}" ]; then
    set -- "$@" --master-passwd "$RVM_MASTER_PASSWD"
fi
if [ "${RVM_PACK_ALL_FHR:-0}" = 1 ]; then
    set -- "$@" --pack-all-fhr
fi
if [ -n "${RVM_IMAGE_SIZE:-}" ]; then
    set -- "$@" --image-size "$RVM_IMAGE_SIZE"
fi
if [ -n "${RVM_SWAP_MIB:-}" ]; then
    set -- "$@" --swap-mib "$RVM_SWAP_MIB"
fi
if [ -n "${RVM_TAP:-}" ]; then
    set -- "$@" --tap "$RVM_TAP"
fi
if [ -n "${RVM_GUEST_IPV4:-}" ]; then
    set -- "$@" --guest-ipv4 "$RVM_GUEST_IPV4"
fi
if [ -n "${RVM_GUEST_GATEWAY:-}" ]; then
    set -- "$@" --guest-gateway "$RVM_GUEST_GATEWAY"
fi
if [ -n "${RVM_GUEST_DNS:-}" ]; then
    set -- "$@" --guest-dns "$RVM_GUEST_DNS"
fi
exec python3 sys/arch/rvm/stand/make_native_root.py "$@"
