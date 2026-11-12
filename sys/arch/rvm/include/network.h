#ifndef _RVM_NETWORK_H_
#define _RVM_NETWORK_H_

/* VMEmu TAP Ethernet, version 1. All registers are 64-bit big endian. */
#define RVM_NET_ADDRESS       0x0700000000004000ULL
#define RVM_NET_MAGIC         0x52564d4e45543031ULL /* RVMNET01 */
#define RVM_NET_VERSION       1
#define RVM_NET_MAX_FRAME     2048
#define RVM_NET_ID            0x00
#define RVM_NET_ABI           0x08
#define RVM_NET_FEATURES      0x10
#define RVM_NET_MAC           0x18 /* six bytes in the low 48 bits */
#define RVM_NET_COMMAND       0x20
#define RVM_NET_STATUS        0x28
#define RVM_NET_ERROR         0x30
#define RVM_NET_BUFFER        0x38 /* physical RAM address */
#define RVM_NET_LENGTH        0x40
#define RVM_NET_RX_COUNT      0x48
#define RVM_NET_TX_COUNT      0x50
#define RVM_NET_F_TAP         1
#define RVM_NET_CMD_RX        1
#define RVM_NET_CMD_TX        2
#define RVM_NET_S_READY       1
#define RVM_NET_S_DONE        2
#define RVM_NET_S_EMPTY       4
#define RVM_NET_S_ERROR       8
#define RVM_NET_E_NONE        0
#define RVM_NET_E_COMMAND     1
#define RVM_NET_E_LENGTH      2
#define RVM_NET_E_BUFFER      3
#define RVM_NET_E_IO          4

#endif
