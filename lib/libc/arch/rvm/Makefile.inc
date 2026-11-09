# Static FHR machine-dependent libc interfaces.
CPPFLAGS+= -I. -I${ARCHDIR}
SRCS+= rvm_sigtramp.S
.PATH: ${ARCHDIR}/gen

# FHR needs local/weak binding metadata until the final static link.
RVM_REAL_CC:= ${CC}
.export RVM_REAL_CC
CC=${NETBSDSRCDIR}/sys/arch/rvm/stand/user_cc.py
# FHR objcopy cannot round-trip relocatables; keep symbols for the adapter.
OBJCOPY=true
# RVM has no historical NetBSD binary ABI. Keep legacy entry points in the
# separate compatibility library rather than the native static libc.
BUILD_LEGACY=yes

# GCC still uses software helpers for binary32, comparisons and conversions.
GENERIC_SRCS+= addsf3.c subsf3.c mulsf3.c divsf3.c extendsfdf2.c truncdfsf2.c
GENERIC_SRCS+= fixdfsi.c fixsfsi.c floatsisf.c floatsidf.c floatunsisf.c floatunsidf.c
GENERIC_SRCS+= comparedf2.c comparesf2.c
SRCS+= rvm_softfloat.c rvm_clzdi2.c rvm_ctzdi2.c
COPTS.rvm_clzdi2.c+= -Wno-missing-prototypes
COPTS.rvm_ctzdi2.c+= -Wno-missing-prototypes
NO_SRCS+= clzdi2.c ctzdi2.c
CPPFLAGS.comparedf2.c+= -U__ELF__ -D__eqdf2=__rvm_eqdf2 -D__nedf2=__rvm_nedf2 -D__ledf2=__rvm_ledf2 -D__ltdf2=__rvm_ltdf2 -D__gedf2=__rvm_gedf2 -D__gtdf2=__rvm_gtdf2 -D__unorddf2=__rvm_unorddf2 -D__cmpdf2=__rvm_cmpdf2
CPPFLAGS.comparesf2.c+= -U__ELF__ -D__eqsf2=__rvm_eqsf2 -D__nesf2=__rvm_nesf2 -D__lesf2=__rvm_lesf2 -D__ltsf2=__rvm_ltsf2 -D__gesf2=__rvm_gesf2 -D__gtsf2=__rvm_gtsf2 -D__unordsf2=__rvm_unordsf2 -D__cmpsf2=__rvm_cmpsf2

# GCC emulated TLS is single-LWP until libpthread's RVM hooks are ported.
SRCS+= gcc_emutls.c
DPSRCS+= gcc_emutls.c
CLEANFILES+= gcc_emutls.c

gcc_emutls.c: ${NETBSDSRCDIR}/external/gpl3/gcc/dist/libgcc/emutls.c \
    ${NETBSDSRCDIR}/sys/arch/rvm/stand/user/emutls.py
	python3 ${NETBSDSRCDIR}/sys/arch/rvm/stand/user/emutls.py \
	    --source ${NETBSDSRCDIR}/external/gpl3/gcc/dist/libgcc/emutls.c \
	    --output ${.TARGET}
COPTS.gcc_emutls.c+= -Wno-pointer-arith
