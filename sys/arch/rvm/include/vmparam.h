

/*
 * NetBSD/RVM - machine-dependent virtual memory parameters.
 *
 * Initial bring-up configuration for the 64-bit RVM virtual CPU.
 *
 * IMPORTANT: The virtual address ranges below are PROVISIONAL.
 * They assume the RVM MMU can translate these addresses and that
 * VMEmu and the kernel bootstrap agree on their interpretation.
 * Normal kernels implement managed user mappings; user executable startup
 * and the process trap/signal ABI are still being brought up.
 */

#ifndef _RVM_VMPARAM_H_
#define _RVM_VMPARAM_H_

#include <machine/param.h>

/* The RVM MMU uses fixed 64 KiB pages. */
#define PAGE_SHIFT PGSHIFT
#define PAGE_SIZE  ( 1UL << PAGE_SHIFT )
#define PAGE_MASK  ( PAGE_SIZE - 1 )

#if PAGE_SHIFT != 16
#error "NetBSD/RVM currently requires 64 KiB MMU pages"
#endif

/*
 * Provisional address-space policy:
 *   user space:  0x0000000000010000 ... 0x00007fffffffffff
 *   kernel VM:   reserved prefix followed by the prebuilt 256 MiB window
 *
 * Kernel text and the RAM identity map still occupy low addresses.
 * The high kernel window below is for dynamically installed wired mappings.
 */
#define VM_MIN_ADDRESS		  ( (vaddr_t)PAGE_SIZE )
#define VM_MAXUSER_ADDRESS	  ( (vaddr_t)0x0000800000000000ULL )
#define VM_MAX_ADDRESS		  VM_MAXUSER_ADDRESS
#define USRSTACK			  VM_MAXUSER_ADDRESS

#define VM_MIN_KERNEL_ADDRESS ( (vaddr_t)0xffff800000000000ULL )
#define VM_MAX_KERNEL_ADDRESS (VM_KERNEL_VM_BASE + VM_KERNEL_VM_SIZE)

/* Leave space below the wired kernel window for the bootstrap scratch VA. */
#define VM_KERNEL_RESERVED	  ( 16UL * 1024 * 1024 )
#define VM_KERNEL_VM_BASE	  ( VM_MIN_KERNEL_ADDRESS + VM_KERNEL_RESERVED )
/* Preallocate all tables for this initial 256 MiB window before UVM. */
#define VM_KERNEL_VM_SIZE	  ( 256UL * 1024 * 1024 )

/* Initial resource limits, subject to tuning. */
#ifndef MAXTSIZ
#define MAXTSIZ ( 128UL * 1024 * 1024 )
#endif
#ifndef DFLDSIZ
#define DFLDSIZ ( 256UL * 1024 * 1024 )
#endif
#ifndef MAXDSIZ
#define MAXDSIZ ( 1024UL * 1024 * 1024 )
#endif
#ifndef DFLSSIZ
#define DFLSSIZ ( 4UL * 1024 * 1024 )
#endif
#ifndef MAXSSIZ
#define MAXSSIZ ( 64UL * 1024 * 1024 )
#endif

/* Match NetBSD's UVM mapping conventions (top-down). */
#define __USE_TOPDOWN_VM
#define VM_DEFAULT_ADDRESS_TOPDOWN( da, sz )  trunc_page ( USRSTACK - MAXSSIZ - ( sz ) - user_stack_guard_size )
#define VM_DEFAULT_ADDRESS_BOTTOMUP( da, sz ) round_page ( (vaddr_t)( da ) + (vsize_t)MAXDSIZ )

/* Physical I/O mapping window and physical memory segment policy. */
#define USRIOSIZE							  64
#define VM_PHYS_SIZE						  ( USRIOSIZE * PAGE_SIZE )
#define VM_PHYSSEG_MAX						  32
#define VM_PHYSSEG_STRAT					  VM_PSTRAT_BSEARCH
#define VM_NFREELIST						  1
#define VM_FREELIST_DEFAULT					  0

/* Initial kernel-map sizing: 4 MiB minimum, bounded by the prebuilt window. */
#define NKMEMPAGES_MIN_DEFAULT (4UL * 1024 * 1024 / PAGE_SIZE)
#define NKMEMPAGES_MAX_DEFAULT (128UL * 1024 * 1024 / PAGE_SIZE)

/* Keep buffer-cache VA inside the prebuilt 256 MiB kernel window. */
#ifndef UBC_WINSHIFT
#define UBC_WINSHIFT 16
#endif
#ifndef UBC_NWINS
#define UBC_NWINS 512
#endif

/* No 32-bit compatibility ABI. The low RAM identity map is temporary. */

#endif /* _RVM_VMPARAM_H_ */
