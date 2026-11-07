/* Assembly callers reserve the homes advertised by the compiler's ABI. */
#ifndef _RVM_ASM_H_
#define _RVM_ASM_H_
#ifdef __RVM_CALLER_ARG_HOME__
#define RVM_C_CALL_HOME __RVM_CALLER_ARG_HOME__
#else
#define RVM_C_CALL_HOME 0
#endif
#endif
