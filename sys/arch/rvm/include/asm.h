/* Assembly callers reserve the homes advertised by the compiler's ABI. */
#ifndef _RVM_ASM_H_
#define _RVM_ASM_H_
#ifdef __RVM_CALLER_ARG_HOME__
#define RVM_C_CALL_HOME __RVM_CALLER_ARG_HOME__
#else
#define RVM_C_CALL_HOME 0
#endif

/* RVM_ASM_NL is expanded to a newline by user_cc.py after preprocessing.
 * A semicolon is a comment character in RVM assembly, not a separator. */
#ifndef _C_LABEL
#define _C_LABEL(x) x
#endif
#define ENTRY(x) .text RVM_ASM_NL .globl x RVM_ASM_NL x:
#define ENTRY_NP(x) ENTRY(x)
#define END(x)
#define WEAK_ALIAS(x,y) .weak x RVM_ASM_NL x = y
#define STRONG_ALIAS(x,y) .globl x RVM_ASM_NL x = y
#define RCSID(x)
#define ASMSTR(x) .asciz x
#define PLT(x) x
#endif
