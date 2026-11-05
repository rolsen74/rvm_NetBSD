/* Version 1 loader/kernel ABI. All fields are big-endian 64-bit words. */
#ifndef _RVM_BOOTINFO_H_
#define _RVM_BOOTINFO_H_

/* "RVMBOOT1", expressed without a suffix for the assembler preprocessor. */
#define RVM_BOOTINFO_MAGIC 5933014506300986417
#define RVM_BOOTINFO_VERSION 1
#define RVM_BOOTINFO_MAXSECTIONS 16
#define RVM_BOOTINFO_SIZE 448
#define RVM_BI_MAGIC 0
#define RVM_BI_VERSION 8
#define RVM_BI_SIZE 16
#define RVM_BI_RAM_START 24
#define RVM_BI_RAM_END 32
#define RVM_BI_LOADED_END 40
#define RVM_BI_NSECTIONS 48
#define RVM_BI_LOADER_STACK 56
#define RVM_BI_SECTIONS 64
#define RVM_BI_SECTION_SIZE 24
#define RVM_BI_SEC_START 0
#define RVM_BI_SEC_SIZE 8
#define RVM_BI_SEC_FLAGS 16

/* FHR section permissions, not MMU PTE bits. */
#define RVM_BOOT_WRITE 1
#define RVM_BOOT_READ 2
#define RVM_BOOT_EXEC 4

#ifndef _LOCORE
#include <sys/types.h>

struct rvm_boot_section {
	uint64_t start;
	uint64_t size;
	uint64_t flags;
};

struct rvm_bootinfo {
	uint64_t magic;
	uint64_t version;
	uint64_t size;
	uint64_t ram_start;      /* v1 supports one contiguous RAM bank at zero */
	uint64_t ram_end;        /* exclusive */
	uint64_t loaded_end;     /* page-rounded end of all loaded sections */
	uint64_t nsections;
	uint64_t loader_stack;   /* first byte of reserved final RAM page */
	struct rvm_boot_section section[RVM_BOOTINFO_MAXSECTIONS];
};

#endif /* !_LOCORE */
#endif /* _RVM_BOOTINFO_H_ */
