/* ELF type definitions are used by MI ksyms headers even with FHR kernels.
 * RVM has no registered ELF machine ID and no ELF executable/module loader.
 * Keep the accepted-machine lists empty; do not borrow another CPU's ID.
 */
#ifndef _RVM_ELF_MACHDEP_H_
#define _RVM_ELF_MACHDEP_H_

#define ARCH_ELFSIZE 64
#define KERN_ELFSIZE 64
#define ELF32_MACHDEP_ENDIANNESS ELFDATA2MSB
#define ELF64_MACHDEP_ENDIANNESS ELFDATA2MSB
#define ELF32_MACHDEP_ID_CASES
#define ELF64_MACHDEP_ID_CASES

#endif
