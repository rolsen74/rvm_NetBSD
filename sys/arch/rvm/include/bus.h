/* MMIO address types. RVM currently has no DMA-capable device drivers. */
#ifndef _RVM_BUS_H_
#define _RVM_BUS_H_
#include <sys/types.h>

typedef uint64_t bus_addr_t;
typedef uint64_t bus_size_t;
typedef uintptr_t bus_space_handle_t;
typedef const struct rvm_bus_space *bus_space_tag_t;

#define PRIxBUSADDR PRIx64
#define PRIxBUSSIZE PRIx64
#define __HAVE_NO_BUS_DMA

#endif
