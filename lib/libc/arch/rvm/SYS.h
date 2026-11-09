/* Native RVM syscall ABI: number R0, arguments R2..R9, carry = error. */
#include <sys/syscall.h>
#include <machine/asm.h>
#define SYSTRAP(x) move.q #SYS_ ## x,r0 RVM_ASM_NL syscall
#define RSYSCALL(x) PSEUDO(x,x)
#define RSYSCALL_NOERROR(x) PSEUDO_NOERROR(x,x)
#define WSYSCALL(weak,strong) WEAK_ALIAS(weak,strong) RVM_ASM_NL PSEUDO(strong,weak)
#define PSEUDO_NOERROR(x,y) ENTRY(x) RVM_ASM_NL SYSTRAP(y) RVM_ASM_NL rts RVM_ASM_NL END(x)
#define PSEUDO(x,y) ENTRY(x) RVM_ASM_NL SYSTRAP(y) RVM_ASM_NL bcs __cerror RVM_ASM_NL rts RVM_ASM_NL END(x)
#define _SYSCALL(x,y) PSEUDO(x,y)
