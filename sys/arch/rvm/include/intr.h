/* UP IPLs. The asynchronous clock is masked at IPL_SCHED and above. */
#ifndef _RVM_INTR_H_
#define _RVM_INTR_H_

#define IPL_NONE 0
#define IPL_SOFTCLOCK 1
#define IPL_SOFTBIO 2
#define IPL_SOFTNET 3
#define IPL_SOFTSERIAL 4
#define IPL_VM 5
#define IPL_SCHED 6
#define IPL_HIGH 7
#define IPL_SAFEPRI IPL_SOFTSERIAL
#define _IPL_N 8

typedef int ipl_t;
typedef struct { ipl_t _spl; } ipl_cookie_t;

#ifdef _KERNEL
int splraise(int);
void splx(int);
void spl0(void);
#define splhigh() splraise(IPL_HIGH)
#define splsched() splraise(IPL_SCHED)
#define splvm() splraise(IPL_VM)
#define splsoftbio() splraise(IPL_SOFTBIO)
#define splsoftclock() splraise(IPL_SOFTCLOCK)
#define splsoftnet() splraise(IPL_SOFTNET)
#define splsoftserial() splraise(IPL_SOFTSERIAL)
#endif

#endif /* _RVM_INTR_H_ */
