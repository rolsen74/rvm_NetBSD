#       $NetBSD: Makefile.inc,v 1.5 2026/10/05 04:43:56 kre Exp $

.PATH:	${.PARSEDIR}

DlfcnMakefileParseDir:=${.PARSEDIR}
CPPFLAGS+=	-I${NETBSDSRCDIR}/libexec/ld.elf_so
CPPFLAGS+=	-I${DlfcnMakefileParseDir}
SRCS+=		dlfcn_elf.c
.if ${MACHINE_ARCH} == "rvm64"
.PATH: ${NETBSDSRCDIR}/sys/arch/rvm/stand/user
SRCS+=		fhr_dl.c
ZLIBSRCDIR:=${NETBSDSRCDIR}/common/dist/zlib
.PATH: ${ZLIBSRCDIR}
CPPFLAGS+=	-I${ZLIBSRCDIR} -DZLIB_CONST -DZ_PREFIX
SRCS+=		adler32.c crc32.c inffast.c inflate.c inftrees.c uncompr.c zutil.c
.endif
