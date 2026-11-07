/* Integer context layout. Process trap/signal entry is still being brought up. */
#ifndef _RVM_MCONTEXT_H_
#define _RVM_MCONTEXT_H_

#include <machine/int_types.h>

#define _NGREG 66
#define _REG_RV 0
#define _REG_FP 62
#define _REG_SP 63
#define _REG_PC 64
#define _REG_SR 65

typedef __uint64_t __greg_t;
typedef __greg_t __gregset_t[_NGREG];
typedef struct {
	__gregset_t __gregs;
} mcontext_t;

#define _UC_SETSTACK 0x00010000
#define _UC_CLRSTACK 0x00020000
#define _UC_TLSBASE  0x00080000

#define _UC_MACHINE_SP(uc) ((uc)->uc_mcontext.__gregs[_REG_SP])
#define _UC_MACHINE_FP(uc) ((uc)->uc_mcontext.__gregs[_REG_FP])
#define _UC_MACHINE_PC(uc) ((uc)->uc_mcontext.__gregs[_REG_PC])
#define _UC_MACHINE_INTRV(uc) ((uc)->uc_mcontext.__gregs[_REG_RV])
#define _UC_MACHINE_SET_PC(uc, pc) (_UC_MACHINE_PC(uc) = (pc))

#endif /* _RVM_MCONTEXT_H_ */
