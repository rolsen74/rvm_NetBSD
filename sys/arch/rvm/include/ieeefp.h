#ifndef _RVM_IEEEFP_H_
#define _RVM_IEEEFP_H_
#include <machine/fenv.h>
typedef int fp_except;
typedef enum { FP_RN=FE_TONEAREST, FP_RZ=FE_TOWARDZERO, FP_RM=FE_DOWNWARD, FP_RP=FE_UPWARD } fp_rnd;
#define FP_X_IMP FE_INEXACT
#define FP_X_UFL FE_UNDERFLOW
#define FP_X_OFL FE_OVERFLOW
#define FP_X_DZ FE_DIVBYZERO
#define FP_X_INV FE_INVALID
#endif
