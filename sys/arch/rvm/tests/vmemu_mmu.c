/* Run the real walker, physical cache, TLB and cache opcodes over test RAM. */
#include "_Data_OS.h"
#include <assert.h>
#include "src_Emulator/Memory/Mem.c"
#include "src_Emulator/Memory/Mem_CacheControl.c"
#include "src_Emulator/Memory/State_Inst_Read.c"
#include "src_Emulator/Memory/State_Data_ReadMem.c"
#include "src_Emulator/Memory/State_Data_WriteMem.c"
#include "src_Emulator/Opcodes/Cache_CInvA.c"
#include "src_Emulator/Opcodes/Cache_CInvL.c"
#include "src_Emulator/Opcodes/Cache_CFlushA.c"
#include "src_Emulator/Opcodes/Cache_CFlushL.c"
#include "src_Emulator/Opcodes/CPU_CPushA.c"
#include "src_Emulator/Opcodes/CPU_CPushL.c"
#include "src_Emulator/Opcodes/CPU_MtCtrl.c"
#include "src_Misc/Nodes_VMAddHead.c"
#include "src_Misc/Nodes_VMAddTail.c"
#include "src_Misc/Nodes_VMRemNode.c"

S32 Option_NoCache, Option_NoInstCache, Option_NoDataCache;
static U8 ram[8 * 1024 * 1024];
static VMCPU cpu;
static VMCPUState state;
static U64 cregs[CReg_RealLast], iregs[64], next_table = 0x10000;
static U8 ibuf[Cache_Inst_Lines][CacheLine_Size], dbuf[Cache_Data_Lines][CacheLine_Size];
static unsigned reads, data_reads, fault;
static U64 fault_address;
enum RetCode Mem_Read_Bytes(VMCPUState *s
#ifdef SHOWMEMACCESSED
    , S32 instruction
#endif
) {
#ifdef SHOWMEMACCESSED
	(void)instruction;
#endif
	assert(s->MemAdr <= sizeof(ram) && s->MemLen <= sizeof(ram) - s->MemAdr);
	reads++; if (s->MemAdr >= 0x500000) data_reads++;
	memcpy(s->MemBuf, ram + s->MemAdr, s->MemLen); return RC_Okay;
}
enum RetCode Mem_Write_Bytes(VMCPUState *s
#ifdef SHOWMEMACCESSED
    , S32 instruction
#endif
) {
#ifdef SHOWMEMACCESSED
	(void)instruction;
#endif
	assert(s->MemAdr <= sizeof(ram) && s->MemLen <= sizeof(ram) - s->MemAdr);
	memcpy(ram + s->MemAdr, s->MemBuf, s->MemLen); return RC_Okay;
}
#define FAULT(name, number) \
enum RetCode name(VMCPUState *s, U64 va, U64 pa) { \
 (void)s; (void)pa; fault = number; fault_address = va; return RC_Handled; }
FAULT(Exce_MMU_Enable, 5)
FAULT(Exce_MMU_Exe, 2)
FAULT(Exce_MMU_Read, 3)
FAULT(Exce_MMU_Write, 4)
enum RetCode Exce_Illegal(VMCPUState *s) { (void)s; fault = 6; return RC_Handled; }
enum RetCode Exce_Privilege(VMCPUState *s) { (void)s; fault = 1; return RC_Handled; }
static U64 load(U64 address) { U64 v; memcpy(&v, ram + address, 8); return BE_SWAP64(v); }
static void store(U64 address, U64 value) { value = BE_SWAP64(value); memcpy(ram + address, &value, 8); }
static U64 table(void) { U64 a = next_table; next_table += 2048; return a; }
static U64 map(U64 root, U64 va, U64 pa, U64 flags) {
	for (int shift = 56; shift > 16; shift -= 8) {
		U64 address = root + ((va >> shift) & 255) * 8, pte = load(address);
		if (!(pte & MMU_PAGE_VALID)) { pte = table() | MMU_PAGE_VALID; store(address, pte); }
		root = pte & ~MMU_MASK_TABEL;
	}
	U64 leaf = root + ((va >> 16) & 255) * 8;
	store(leaf, pa | flags | MMU_PAGE_VALID); return leaf;
}
static enum RetCode fetch(U64 va, U8 *byte) { state.PC = state.InstAdr = va; return State_Inst_Read(&state, byte, 1); }
static void mode(int supervisor) { state.SR = cregs[CReg_SR] = supervisor ? SRF_FlagS : 0; }
static enum RetCode opcode(enum RetCode (*fn)(VMCPUState *), U8 type, int reg) {
	ram[0x300000] = 1; ram[0x300001] = type; ram[0x300002] = reg;
	state.PC = state.InstAdr = 0x300000;
	U64 old = cregs[CReg_CR]; cregs[CReg_CR] = 0;
	enum RetCode rc = fn(&state); cregs[CReg_CR] = old; return rc;
}
int main(void) {
	cpu.cpu_ControlRegsPtr = cregs; cpu.cpu_IntegerRegsPtr = iregs; state.cpu = &cpu;
	for (unsigned i = 0; i < Cache_Inst_Lines; i++) {
		cpu.cpu_ICache[i].cl_Addr_Virt = ~0ULL; cpu.cpu_ICache[i].cl_Buffer = ibuf[i];
		VMAddTail(&cpu.cpu_ICacheHeader, &cpu.cpu_ICache[i]);
	}
	for (unsigned i = 0; i < Cache_Data_Lines; i++) {
		cpu.cpu_DCache[i].cl_Addr_Virt = ~0ULL; cpu.cpu_DCache[i].cl_Buffer = dbuf[i];
		VMAddTail(&cpu.cpu_DCacheHeader, &cpu.cpu_DCache[i]);
	}
	U64 a = table(), b = table(), va = 0x400000, pa = 0x500000, pb = 0x600000;
	U64 flags = MMU_PAGE_READWRITE | MMU_PAGE_EXECUTABEL | MMU_CACHE_WRITETHROUGH;
	U64 leaf = map(a, va, pa, flags);
	map(b, va, pb, flags); map(a, va + 65536, pb, flags);
	cregs[CReg_MUT] = a; cregs[CReg_MST] = b;
	cregs[CReg_CR] = CRF_MMU | CRF_InstCache | CRF_TLB;
	ram[pa] = 0xa1; ram[pb] = 0xb2;
	U8 byte; mode(0);
	assert(fetch(va, &byte) == RC_Okay && byte == 0xa1);
	unsigned before = reads;
	assert(fetch(va, &byte) == RC_Okay && byte == 0xa1 && reads == before);
	cregs[CReg_MUT] = b; assert(fetch(va, &byte) == RC_Okay && byte == 0xb2);
	cregs[CReg_MUT] = a; mode(1); assert(fetch(va, &byte) == RC_Okay && byte == 0xb2);
	mode(0); assert(fetch(va, &byte) == RC_Okay && byte == 0xa1);
	store(leaf, pb | flags | MMU_PAGE_VALID);
	assert(fetch(va, &byte) == RC_Okay && byte == 0xa1); /* deliberately stale until invalidation */
	mode(1); iregs[2] = va; assert(opcode(OPCODE_CINVL, 4, 2) == RC_Okay); mode(0);
	assert(fetch(va, &byte) == RC_Okay && byte == 0xb2);
	store(leaf, pa | MMU_PAGE_VALID | MMU_PAGE_READABLE);
	Mem_InvalidateTLB(&cpu, 1, 0); before = data_reads;
	assert(fetch(va, &byte) == RC_Handled && fault == 2 && data_reads == before);
	byte = 9; assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Handled && fault == 4);
	store(leaf, pa | flags | MMU_PAGE_VALID | MMU_PAGE_SUPER); Mem_InvalidateTLB(&cpu, 1, 0);
	assert(fetch(va, &byte) == RC_Handled && fault == 6);
	store(leaf, pa | flags | MMU_PAGE_VALID); Mem_InvalidateTLB(&cpu, 1, 0);
	assert(State_Data_ReadMem(&state, va, 1, &byte) == RC_Okay);
	assert((load(leaf) & MMU_USED_READ) && !(load(leaf) & MMU_USED_WRITE));
	byte = 0xa1; assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Okay);
	assert((load(leaf) & MMU_USED_WRITE) && (load(a) & MMU_USED_WRITE));
	before = reads;
	assert(State_Data_ReadMem(&state, va, 1, &byte) == RC_Okay && reads == before + 1);
	before = reads;
	assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Okay && reads == before);
	assert(fetch(va, &byte) == RC_Okay && byte == 0xa1);
	ram[pa] = 0xa2; assert(fetch(va, &byte) == RC_Okay && byte == 0xa1);
	mode(1); iregs[2] = pa; assert(opcode(OPCODE_CINVL, 2, 2) == RC_Okay); mode(0);
	assert(fetch(va, &byte) == RC_Okay && byte == 0xa2);
	ram[pa + 1024] = 0xc1;
	assert(fetch(va + 1024, &byte) == RC_Okay && byte == 0xc1);
	ram[pa + 1024] = 0xc2;
	mode(1); iregs[2] = pa; assert(opcode(OPCODE_CINVL, 2, 2) == RC_Okay); mode(0);
	assert(fetch(va + 1024, &byte) == RC_Okay && byte == 0xc1);
	mode(1); iregs[2] = pa + 1024; assert(opcode(OPCODE_CFLUSHL, 2, 2) == RC_Okay); mode(0);
	assert(fetch(va + 1024, &byte) == RC_Okay && byte == 0xc2);
	assert(fetch(va, &byte) == RC_Okay && byte == 0xa2);
	map(a, va + 2 * 65536, pa, flags);
	before = data_reads;
	assert(fetch(va + 2 * 65536, &byte) == RC_Okay && byte == 0xa2 && data_reads == before);
	U8 pair[2]; ram[pa + 65535] = 0x12; ram[pb] = 0x34;
	assert(State_Data_ReadMem(&state, va + 65535, 2, pair) == RC_Okay && pair[0] == 0x12 && pair[1] == 0x34);
	map(a, va + 65536, pb, 0); Mem_InvalidateTLB(&cpu, 1, 0);
	assert(State_Data_ReadMem(&state, va + 65535, 2, pair) == RC_Handled && fault_address == va + 65536);
	/* Dirty discard must not write back later; flush must write back. */
	store(leaf, pa | MMU_PAGE_VALID | MMU_PAGE_READWRITE | MMU_CACHE_WRITEBACK);
	Mem_InvalidateTLB(&cpu, 1, 0); cregs[CReg_CR] |= CRF_DataCache; byte = 0x77;
	assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Okay && ram[pa] == 0xa2);
	mode(1); assert(opcode(OPCODE_CINVA, 1, 0) == RC_Okay); mode(0);
	assert(State_Data_ReadMem(&state, va, 1, &byte) == RC_Okay && byte == 0xa2);
	byte = 0x88; assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Okay);
	mode(1); iregs[2] = pa; assert(opcode(OPCODE_CPUSHL, 1, 2) == RC_Okay && ram[pa] == 0x88);
	mode(0); byte = 0x99; assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Okay);
	mode(1); assert(opcode(OPCODE_CFLUSHL, 1, 2) == RC_Okay && ram[pa] == 0x99);
	assert(opcode(OPCODE_CFLUSHA, 7, 0) == RC_Okay);
	assert(opcode(OPCODE_CINVA, 8, 0) == RC_Handled && fault == 6);
	mode(0); assert(opcode(OPCODE_CINVA, 2, 0) == RC_Handled && fault == 1);
	/* Guest root writes and TLB enable transitions flush even same-root entries. */
	mode(1); cregs[CReg_CR] = CRF_MMU | CRF_TLB; cpu.cpu_TLB[0].valid = 1;
	map(b, 0x300000, 0x300000, MMU_PAGE_READABLE | MMU_PAGE_EXECUTABEL);
	ram[0x300000] = 2; ram[0x300001] = 0; ram[0x300002] = CReg_MUT; iregs[2] = a;
	state.PC = state.InstAdr = 0x300000;
	assert(OPCODE_MTCTRL(&state) == RC_Okay);
	for (unsigned i = 0; i < VM_TLB_ENTRIES; i++) assert(!cpu.cpu_TLB[i].valid);
	ram[0x300002] = CReg_CR; iregs[2] = CRF_MMU;
	state.PC = state.InstAdr = 0x300000;
	assert(OPCODE_MTCTRL(&state) == RC_Okay && cregs[CReg_CR] == CRF_MMU);
	for (unsigned i = 0; i < VM_TLB_ENTRIES; i++) assert(!cpu.cpu_TLB[i].valid);
	mode(0); assert(State_Data_ReadMem(&state, va, 1, &byte) == RC_Okay && byte == 0x99);
	store(leaf, pb | MMU_PAGE_VALID | MMU_PAGE_READWRITE);
	assert(State_Data_ReadMem(&state, va, 1, &byte) == RC_Okay && byte == 0x34);
	/* Write-through must not overwrite unrelated bytes changed by DMA. */
	store(leaf, pa | MMU_PAGE_VALID | MMU_PAGE_READWRITE | MMU_CACHE_WRITETHROUGH);
	cregs[CReg_CR] = CRF_MMU | CRF_DataCache;
	assert(State_Data_ReadMem(&state, va, 1, &byte) == RC_Okay);
	ram[pa + 5] = 0xaa; byte = 0x44;
	assert(State_Data_WriteMem(&state, va, 1, &byte) == RC_Okay && ram[pa] == 0x44 && ram[pa + 5] == 0xaa);
	puts("PASS: physical cache aliases, root/mode TLB tags, R/M updates, NX/protection, invalidation and privilege");
	return 0;
}
