/* Integer register access. Single-step and floating-point requests pending. */
#ifndef _RVM_PTRACE_H_
#define _RVM_PTRACE_H_

#include <machine/reg.h>
#define PT_GETREGS (PT_FIRSTMACH + 0)
#define PT_SETREGS (PT_FIRSTMACH + 1)
#define PT_MACHDEP_STRINGS "PT_GETREGS", "PT_SETREGS"
#define PTRACE_REG_PC(r) ((r)->r_pc)
#define PTRACE_REG_SET_PC(r, v) ((r)->r_pc = (v))
#define PTRACE_REG_FP(r) ((r)->r_regs[RVM_REG_FP])
#define PTRACE_REG_SP(r) ((r)->r_regs[RVM_REG_SP])
#define PTRACE_REG_INTRV(r) ((r)->r_regs[RVM_REG_RET0])

#endif
