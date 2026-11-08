/* GeneralDriver console and tty interfaces. */
#ifndef _RVM_CONSOLE_H_
#define _RVM_CONSOLE_H_
#include <sys/types.h>
void rvm_console_attach(void);
void rvm_console_poll(bool);
#endif
