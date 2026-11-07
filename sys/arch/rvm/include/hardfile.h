/* RVM hardfile MMIO ABI v1. Registers are aligned, big-endian 64-bit words. */
#ifndef RVM_HARDFILE_H
#define RVM_HARDFILE_H
#define RVM_HF_DEFAULT_ADDRESS 0x0700000300000000ULL
#define RVM_HF_MAGIC 0x52564d4844303031ULL /* RVMHD001 */
#define RVM_HF_VERSION 1
#define RVM_HF_SECTOR_SIZE 512
#define RVM_HF_MAX_TRANSFER (1024 * 1024)
#define RVM_HF_ID 0x00
#define RVM_HF_ABI 0x08
#define RVM_HF_FEATURES 0x10
#define RVM_HF_SECTOR_BYTES 0x18
#define RVM_HF_SECTORS 0x20
#define RVM_HF_COMMAND 0x28
#define RVM_HF_STATUS 0x30
#define RVM_HF_ERROR 0x38
#define RVM_HF_LBA 0x40
#define RVM_HF_COUNT 0x48
#define RVM_HF_BUFFER 0x50
#define RVM_HF_TRANSFERRED 0x58
#define RVM_HF_IRQ_ENABLE 0x60
#define RVM_HF_IRQ_STATUS 0x68 /* write 1 to acknowledge */
#define RVM_HF_MAX_BYTES 0x70
#define RVM_HF_F_WRITABLE 1
#define RVM_HF_F_INTERRUPT 2
#define RVM_HF_F_FLUSH 4
#define RVM_HF_CMD_READ 1
#define RVM_HF_CMD_WRITE 2
#define RVM_HF_CMD_FLUSH 3
#define RVM_HF_S_READY 1
#define RVM_HF_S_DONE 2
#define RVM_HF_S_ERROR 4
#define RVM_HF_S_BUSY 8
#define RVM_HF_E_NONE 0
#define RVM_HF_E_COMMAND 1
#define RVM_HF_E_RANGE 2
#define RVM_HF_E_BUFFER 3
#define RVM_HF_E_READONLY 4
#define RVM_HF_E_IO 5
#define RVM_HF_E_REGISTER 6
#endif
