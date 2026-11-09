/* GCC lowers 32-bit count builtins to the word-mode helper on RVM. */
extern int __ctzsi2(unsigned);
#define __builtin_ctz __ctzsi2
#define __ctzdi2 __rvm_ctzdi2
#include "../../../../../sys/external/bsd/compiler_rt/dist/lib/builtins/ctzdi2.c"
