/* Compile-only checks against the actual cross compiler, never host headers. */
#include <sys/types.h>
#include <sys/inttypes.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <machine/reg.h>
#include <machine/pcb.h>
#include <machine/switch.h>
#include <machine/frame.h>
#include <machine/generic.h>
#include <machine/boot.h>
#include <machine/bootinfo.h>
#include <machine/mmu.h>
#include <machine/mcontext.h>
#include <machine/signal.h>
#include <machine/limits.h>

#if !defined(__rvm__)
#error "Use the RVM cross compiler"
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "NetBSD/RVM requires big-endian code generation"
#endif

#define CHECK_SIZE(type, size) \
	_Static_assert(sizeof(type) == (size), #type " ABI size")

CHECK_SIZE(char, 1);
CHECK_SIZE(short, 2);
CHECK_SIZE(int, 4);
CHECK_SIZE(long, 8);
CHECK_SIZE(void *, 8);
CHECK_SIZE(__int8_t, 1);
CHECK_SIZE(__uint8_t, 1);
CHECK_SIZE(__int16_t, 2);
CHECK_SIZE(__uint16_t, 2);
CHECK_SIZE(__int32_t, 4);
CHECK_SIZE(__uint32_t, 4);
CHECK_SIZE(__int64_t, 8);
CHECK_SIZE(__uint64_t, 8);
CHECK_SIZE(__intptr_t, sizeof(void *));
CHECK_SIZE(int_least64_t, 8);
CHECK_SIZE(int_fast32_t, 4);
CHECK_SIZE(int_fast64_t, 8);
CHECK_SIZE(intmax_t, 8);
CHECK_SIZE(register_t, 8);
CHECK_SIZE(paddr_t, 8);
CHECK_SIZE(vaddr_t, 8);
CHECK_SIZE(struct reg, 66 * 8);
CHECK_SIZE(mcontext_t, sizeof(struct reg));
CHECK_SIZE(label_t, 66 * 8);
CHECK_SIZE(sig_atomic_t, 4);
_Static_assert((sig_atomic_t)-1 < 0, "signed sig_atomic_t");
_Static_assert(INT_MAX == 2147483647 && LONG_MAX == 9223372036854775807L,
    "RVM MI integer limits");
_Static_assert(__builtin_offsetof(mcontext_t, __gregs[_REG_PC]) ==
    __builtin_offsetof(struct reg, r_pc), "integer context PC layout");
CHECK_SIZE(struct rvm_generic_timer, 4 * 8);
CHECK_SIZE(struct rvm_bootinfo, RVM_BOOTINFO_SIZE);
CHECK_SIZE(struct rvm_boot_section, RVM_BI_SECTION_SIZE);
_Static_assert(__builtin_offsetof(struct rvm_bootinfo, ram_end) == RVM_BI_RAM_END,
    "loader RAM offset");
_Static_assert(__builtin_offsetof(struct rvm_bootinfo, section) == RVM_BI_SECTIONS,
    "loader section table offset");
_Static_assert(__builtin_offsetof(struct rvm_boot_section, flags) == RVM_BI_SEC_FLAGS,
    "loader permission offset");
_Static_assert(RVM_MMU_TABLE_SIZE == RVM_MMU_ENTRIES * sizeof(uint64_t),
    "hardware table size");
_Static_assert(RVM_MMU_PAGE_SIZE == PAGE_SIZE, "hardware page size");
_Static_assert(__builtin_offsetof(struct rvm_general_driver, term_control)
    == 40 * 8, "GeneralDriver terminal control offset");
_Static_assert(__builtin_offsetof(struct rvm_general_driver, term_putchar)
    == 45 * 8, "GeneralDriver character output offset");
_Static_assert(__builtin_offsetof(struct rvm_general_driver, video_control)
    == 64 * 8, "GeneralDriver video control offset");
_Static_assert(RVM_BOOT_STACK_SIZE == USPACE, "startup stack allocation");

_Static_assert((__int8_t)-1 < 0, "int8 must be signed despite unsigned char ABI");
_Static_assert(__alignof__(long) == 4, "GCC scalar alignment changed");
_Static_assert(STACK_ALIGNBYTES == 3, "GCC stack ABI is 4-byte aligned");
_Static_assert(PAGE_SIZE == 65536, "RVM hardware page size");
_Static_assert(_BYTE_ORDER == _BIG_ENDIAN, "NetBSD byte order");
_Static_assert(__ALIGNBYTES == 3, "NetBSD scalar alignment");
_Static_assert(INT64_MAX == 0x7fffffffffffffffL, "signed 64-bit limit");
_Static_assert(UINT64_MAX == UINT64_C(0xffffffffffffffff), "unsigned limit");
_Static_assert(INT_FAST32_MAX == 0x7fffffff, "fast integer limit");
_Static_assert(WCHAR_MIN == 0 && WCHAR_MAX == 0xffffffffU, "wchar ABI");
_Static_assert(USPACE == 2 * PAGE_SIZE, "kernel stack allocation");
_Static_assert(__builtin_offsetof(struct reg, r_pc) == 512, "saved PC offset");
_Static_assert(__builtin_offsetof(struct trapframe, tf_usp) == 504,
    "trapframe integer register area");
_Static_assert(__builtin_offsetof(struct pcb, pcb_usp) == 512,
    "PCB integer register area");

/* Format checking must agree with the chosen integer typedefs. */
extern void rvm_check_printf(const char *, ...) __printflike(1, 2);

void
rvm_check_formats(int64_t s, uint64_t u, uintptr_t p, intmax_t m)
{
	rvm_check_printf("%" PRId64 " %" PRIx64 " %" PRIxPTR " %" PRIdMAX,
	    s, u, p, m);
}

_Static_assert(__builtin_offsetof(struct pcb, pcb_gpr[RVM_REG_SP]) == RVM_PCB_SP, "switch SP");
_Static_assert(__builtin_offsetof(struct pcb, pcb_ssp) == RVM_PCB_SSP, "switch SSP");
_Static_assert(__builtin_offsetof(struct pcb, pcb_pc) == RVM_PCB_PC, "switch PC");
_Static_assert((RVM_SWITCH_LAST - RVM_SWITCH_FIRST + 1) * 8 == RVM_SWITCH_PC, "switch registers");
_Static_assert(RVM_SWITCH_PC + 8 == RVM_SWITCH_SIZE, "switch frame");
