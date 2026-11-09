# FHR has no ELF init arrays, dynamic startup, profiling or unwind frames.
RVM_STATIC_FHR=yes
RVM_REAL_CC:= ${CC}
.export RVM_REAL_CC
CC=${NETBSDSRCDIR}/sys/arch/rvm/stand/user_cc.py
CPPFLAGS+= -I${.CURDIR}/common
OBJCOPY=true

OBJS= crt0.o crtbegin.o crtend.o
realall: ${OBJS}

crt0.o: crt0.S crt0.c ${NETBSDSRCDIR}/sys/arch/rvm/stand/fhr_csu.py
	${_MKTARGET_COMPILE}
	python3 ${NETBSDSRCDIR}/sys/arch/rvm/stand/fhr_csu.py \
	    --cc ${RVM_REAL_CC} --entry ${ARCHDIR}/crt0.S \
	    --source ${ARCHDIR}/crt0.c -o ${.TARGET} -- ${CFLAGS} ${CPPFLAGS}

FILES= ${OBJS}
FILESDIR= ${LIBDIR}
CLEANFILES+= ${OBJS} *.raw.s *.entry.s *.fhr.s *.weak.json *.init.json
.include <bsd.prog.mk>
