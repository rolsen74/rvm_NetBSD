/* The MI call-graph recorder uses the ordinary two-argument C ABI.
 * RVM GCC does not yet generate profiling calls (-pg is unsupported).
 */
#ifndef _RVM_PROFILE_H_
#define _RVM_PROFILE_H_
#define _MCOUNT_DECL void _mcount
#endif
