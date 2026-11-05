/* NetBSD/RVM standalone startup interfaces. */
#ifndef _RVM_BOOT_H_
#define _RVM_BOOT_H_

#define RVM_BOOT_STACK_SIZE 131072
#define RVM_BOOT_NVECTORS 96

#ifndef _LOCORE
#include <sys/types.h>
#include <machine/bootinfo.h>

extern volatile unsigned long rvm_boot_stage;
extern char rvm_boot_stack[RVM_BOOT_STACK_SIZE];
#define rvm_boot_stack_top (rvm_boot_stack + RVM_BOOT_STACK_SIZE)
extern void (*rvm_boot_vectors[RVM_BOOT_NVECTORS])(void);

void rvm_early_init(const struct rvm_bootinfo *);
void rvm_early_consinit(void);
void rvm_early_putchar(int);
void rvm_early_puts(const char *);
void rvm_early_puthex(uint64_t);
void rvm_emulator_putchar(int);
void rvm_boot_trap(const uint64_t *) __dead;
void rvm_boot_stop(void) __dead;
uint64_t rvm_boot_get_sr(void);

#endif /* !_LOCORE */
#endif /* _RVM_BOOT_H_ */
