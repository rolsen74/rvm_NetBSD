/* Integer and FPU register access. Single-step remains unimplemented. */
#ifndef _RVM_PTRACE_H_
#define _RVM_PTRACE_H_

#include <machine/reg.h>
#define PT_GETREGS (PT_FIRSTMACH + 0)
#define PT_SETREGS (PT_FIRSTMACH + 1)
#define PT_GETFPREGS (PT_FIRSTMACH + 2)
#define PT_SETFPREGS (PT_FIRSTMACH + 3)
#define PT_MACHDEP_STRINGS "PT_GETREGS", "PT_SETREGS", "PT_GETFPREGS", "PT_SETFPREGS"
#define PTRACE_REG_PC(r) ((r)->r_pc)
#define PTRACE_REG_SET_PC(r, v) ((r)->r_pc = (v))
#define PTRACE_REG_FP(r) ((r)->r_regs[RVM_REG_FP])
#define PTRACE_REG_SP(r) ((r)->r_regs[RVM_REG_SP])
#define PTRACE_REG_INTRV(r) ((r)->r_regs[RVM_REG_RET0])

#endif
