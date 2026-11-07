/* Native siginfo-v2 delivery; sig_atomic_t follows the RVM integer ABI. */
#ifndef _RVM_SIGNAL_H_
#define _RVM_SIGNAL_H_

#ifndef _LOCORE
typedef int sig_atomic_t;
#endif

#endif /* _RVM_SIGNAL_H_ */
