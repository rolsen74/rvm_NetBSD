/* Standalone VA-only reservations using the real MI UVM map code. */
#ifndef _RVM_BOOTMAP_H_
#define _RVM_BOOTMAP_H_

#include <sys/types.h>

int rvm_bootmap_init(void);
/* Page-aligned size; hint is in/out. Fixed requests require alignment == 0.
 * Non-fixed requests require a power-of-two alignment >= PAGE_SIZE.
 * Returns an errno. Reservations have no backing object or physical pages.
 */
int rvm_bootmap_reserve(vaddr_t *, vsize_t, vsize_t, int);
struct uvm_object;
int rvm_bootmap_reserve_object(vaddr_t *, vsize_t, struct uvm_object *);
/* Exact entries only; rejects partial, unknown or double releases. */
int rvm_bootmap_release(vaddr_t, vsize_t);

#endif
