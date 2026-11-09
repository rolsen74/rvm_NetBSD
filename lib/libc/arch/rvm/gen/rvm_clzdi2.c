/* GCC lowers 32-bit count builtins to the word-mode helper on RVM. */
extern int __clzsi2(unsigned);
#define __builtin_clz __clzsi2
#define __clzdi2 __rvm_clzdi2
#include "../../../../../sys/external/bsd/compiler_rt/dist/lib/builtins/clzdi2.c"
