/* Check actual entropy MMIO callbacks, including host-provider failures. */
#include "_Data_Plugin.h"
#include <sys/random.h>
#include <assert.h>
static int kind, calls;
ssize_t __real_getrandom(void *, size_t, unsigned);
ssize_t __wrap_getrandom(void *buf, size_t size, unsigned flags) {
	calls++;
	if (kind == 0) return __real_getrandom(buf, size, flags);
	if (kind == 1) { errno = EIO; return -1; }
	if (kind == 2) return 0;
	if (kind == 3 && calls == 1) { errno = EINTR; return -1; }
	if (kind == 3 && size > 3) size = 3;
	memset(buf, kind == 4 ? 0 : 0x5a, size); return size;
}
U64 myWin_Keyboard_GetKey(VMContext *ctx) { (void)ctx; return 0; }
U64 Generic_Clock_Read(DRVPlugin *pd, enum IOCmdID reg) { (void)pd; (void)reg; return 0; }
void Misc_SendCommand(struct GUICmd_Message *msg) { (void)msg; assert(0 && "entropy register write queued GUI work"); }
S32 CPU_Running = TRUE;
static U64 read_reg(VMMemory *mem, int reg) {
	U8 data[9]; U64 value;
	myGeneric_ReadMem2(NULL, mem, data + 1, reg * 8, 8);
	memcpy(&value, data + 1, 8); return BE_SWAP64(value);
}
int main(void) {
	DRVPlugin pd = {0}; VMMemory mem = {0}; mem.mem_Plugin = &pd; mem.mem_MemSize = 4096;
	assert(read_reg(&mem, cmdid_EntropyStatus) == 1);
	kind = 3; calls = 0;
	assert(read_reg(&mem, cmdid_EntropyData) == 0x5a5a5a5a5a5a5a5aULL && calls == 4);
	assert(read_reg(&mem, cmdid_EntropyStatus) == 3);
	kind = 1; assert(read_reg(&mem, cmdid_EntropyData) == 0);
	assert(read_reg(&mem, cmdid_EntropyStatus) == 1);
	kind = 2; assert(read_reg(&mem, cmdid_EntropyData) == 0 && read_reg(&mem, cmdid_EntropyStatus) == 1);
	kind = 4; assert(read_reg(&mem, cmdid_EntropyData) == 0 && read_reg(&mem, cmdid_EntropyStatus) == 3);
	U64 data = ~0ULL;
	myGeneric_WriteMem2(NULL, &mem, (U8 *)&data, cmdid_EntropyData * 8, 8);
	myGeneric_WriteMem2(NULL, &mem, (U8 *)&data, cmdid_EntropyStatus * 8, 8);
	assert(read_reg(&mem, cmdid_EntropyStatus) == 3);
	kind = 0; (void)read_reg(&mem, cmdid_EntropyData); assert(read_reg(&mem, cmdid_EntropyStatus) == 3);
	puts("PASS: entropy MMIO, secure host provider, EINTR/partial reads, failure status and valid zero");
	return 0;
}
