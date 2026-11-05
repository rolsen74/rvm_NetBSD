#!/usr/bin/env python3
"""Exercise the real RVM loader, compiler, allocator and MMU in VMEmu."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objdir', type=Path, required=True)
    parser.add_argument('--cross', required=True)
    parser.add_argument('--vmasm', default='VMAsm')
    parser.add_argument('--vmemu', default='VMEmu')
    args = parser.parse_args()
    obj = args.objdir.resolve()
    root = Path(__file__).resolve().parents[4]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    ready = 'UVM page allocator ready; allocation/free verified.'
    preload_ready = 'UVM physical memory registered; bootstrap allocation verified.'
    compiler = [args.cross + 'gcc',
                '-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-fno-pic', '-fno-pie',
                '-fno-merge-constants', '-nostdinc', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-D_KERNEL', '-I' + str(obj / 'include'),
                '-I' + str(root / 'sys'), '-I' + str(root / 'common/include')]

    def run(directory):
        try:
            result = subprocess.run([args.vmemu, '-c', 'vm.config'], cwd=directory,
                                    env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=5)
            raise AssertionError('VM exited unexpectedly: ' + str(result.returncode)
                                 + '\n' + result.stdout.decode(errors='replace')[:12000])
        except subprocess.TimeoutExpired as exc:
            return exc.stdout.decode(errors='replace')

    def configure(directory, pages):
        config = (obj / 'vm.config').read_text()
        config = re.sub(r'Size = \d+', 'Size = ' + str(pages), config, count=1)
        (directory / 'vm.config').write_text(config)
        shutil.copyfile(obj / 'bootloader.bin', directory / 'bootloader.bin')

    with tempfile.TemporaryDirectory(prefix='netbsd-rvm-test-') as tmp:
        work = Path(tmp)
        # FHR REL records keep addends in the instruction bytes. Losing them
        # sends data references and local jumps to the starts of sections.
        reloc_source = '''.text
.global _start
_start:
 lea reloc_data,r2
 lea reloc_data+8,r3
 jmp .Lreloc_target
 nop
.Lreloc_target:
 rts
.data
 .quad 0
.global reloc_data
reloc_data:
 .quad 1
 .quad 2
'''
        (work / 'reloc.s').write_text(reloc_source)
        subprocess.run([args.cross + 'gcc', '-nostdinc', '-x', 'assembler',
                        '-c', str(work / 'reloc.s'), '-o', str(work / 'reloc.o')], check=True)
        subprocess.run([args.cross + 'ld', '-e', '_start', '-o',
                        str(work / 'reloc.fhr'), str(work / 'reloc.o')],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        image = (work / 'reloc.fhr').read_bytes()
        assert image[:8] == b'FHR\0\x01\x01\x00\x00', 'unexpected FHR encoding'
        count = int.from_bytes(image[14:16], 'big')
        chunk = 16 + 12 * count
        assert image[chunk:chunk + 4] == b'COD\0', 'missing first code section'
        text = image[chunk + 14:chunk + 14 + int.from_bytes(image[16:24], 'big')]
        actual = [int.from_bytes(text[offset:offset + 8], 'big') for offset in (3, 16, 29)]
        assert actual == [8, 16, 39], f'FHR linker lost in-place addends: {actual}'
        print('PASS: linker preserves data offsets, explicit addends and local jump targets', flush=True)

        # The free range must change with RAM size, without including the image,
        # bootstrap tables, scratch page or the loader's final stack page.
        shutil.copyfile(obj / 'netbsd.fhr', work / 'netbsd.fhr')
        for pages in (16, 256, 1024):
            configure(work, pages)
            out = run(work)
            assert ready in out and 'fatal startup exception' not in out, out
            reserved = int(re.search(r'Reserved prefix end: 0x([0-9a-f]+)', out)[1], 16)
            rootpa = int(re.search(r'Supervisor MMU root: 0x([0-9a-f]+)', out)[1], 16)
            free = re.search(r'Free physical range for UVM: 0x([0-9a-f]+)\.\.0x([0-9a-f]+)', out)
            first, last = (int(value, 16) for value in free.groups())
            assert reserved <= rootpa < first < last == (pages - 1) * 65536, out
            assert rootpa % 2048 == first % 65536 == last % 65536 == 0, out
            registered = re.search(r'UVM registered PFN range: 0x([0-9a-f]+)\.\.0x([0-9a-f]+)', out)
            assert tuple(int(value, 16) for value in registered.groups()) == (first >> 16, last >> 16), out
            assert f'UVM bootstrap allocation: 0x{first:016x}' in out, out
            print(f'PASS: RAM {pages * 64} KiB, reservations, allocation, translated alias', flush=True)

        configure(work, 10)
        out = run(work)
        assert 'Cannot build or verify bootstrap page tables.' in out and ready not in out, out
        print('PASS: insufficient RAM stops before MMU activation', flush=True)

        def build_wrapper(body, assembly='', preload=False):
            source = '#define RVM_TEST_PRELOAD_ONLY\n' if preload else ''
            source += '#define rvm_early_init rvm_normal_early_init\n'
            source += '#include "' + str(root / 'sys/arch/rvm/rvm/machdep.c') + '"\n'
            source += '#undef rvm_early_init\n' + body
            (work / 'wrapper.c').write_text(source)
            subprocess.run(compiler + ['-c', str(work / 'wrapper.c'), '-o', str(work / 'machdep.o')], check=True)
            objects = [obj / 'locore.o', work / 'machdep.o', obj / 'earlycons.o',
                       obj / 'bootmem.o', obj / 'pmap.o', obj / 'uvm_machdep.o',
                       obj / 'uvm_bootstrap.o', obj / 'uvm_support.o']
            objects += [obj / (name + '.o') for name in
                        ('uvm_physseg', 'uvm_page', 'uvm_extent', 'uvm_clock',
                         'uvm_meter', 'uvm_pgflcache')]
            if assembly:
                asm = ' section ".text",code\n xdef rvm_test_probe\nrvm_test_probe:\n' + assembly
                (work / 'probe.vma').write_text(asm)
                subprocess.run([args.vmasm, '-i', str(work / 'probe.vma'),
                                '-o', str(work / 'probe.o'), '--obj'], check=True, stdout=subprocess.DEVNULL)
                objects.append(work / 'probe.o')
            subprocess.run([args.cross + 'ld', '-e', '_start', '-o', str(work / 'netbsd.fhr')]
                           + [str(path) for path in objects], check=True)
            configure(work, 256)

        # Exercise unsigned comparison branches and symbol addresses at -O2.
        comparisons = '''
volatile unsigned long test_a __attribute__((section(".data"))) = 0;
volatile unsigned long test_b __attribute__((section(".data"))) = 0;
const char cmp_fail[] = "Compiler comparison regression failed.\\n";
const char cmp_pass[] = "Compiler comparisons verified.\\n";
__attribute__((noinline)) int compare(void) {
    return (test_a < test_b) | ((test_a >= test_b) << 1) |
        ((test_a > test_b) << 2) | ((test_a <= test_b) << 3);
}
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_early_consinit();
'''
        for a, b in ((0, 0), (0, 1), (1, 0), (2**64 - 1, 0),
                     (0, 2**64 - 1), (2**63, 2**63 - 1), (2**63 - 1, 2**63)):
            expected = int(a < b) | (int(a >= b) << 1) | (int(a > b) << 2) | (int(a <= b) << 3)
            comparisons += f'test_a = {a}UL; test_b = {b}UL;\n'
            comparisons += f'if (compare() != {expected}) {{ rvm_early_puts(cmp_fail); rvm_boot_stop(); }}\n'
        comparisons += 'rvm_early_puts(cmp_pass); rvm_normal_early_init(bi); }\n'
        build_wrapper(comparisons)
        out = run(work)
        assert 'Compiler comparisons verified.' in out and ready in out, out
        print('PASS: compiler unsigned branches and symbol-address arithmetic', flush=True)

        boundary = '''
#include <uvm/uvm.h>
const char rvm_cross_ok[] = "MMU page boundary verified.\\n";
const char rvm_cross_bad[] = "MMU page boundary regression.\\n";
void rvm_early_init(const struct rvm_bootinfo *bi) {
    rvm_normal_early_init(bi);
    struct vm_page *a = uvm_pagealloc_strat(0, 0, 0, UVM_PGA_USERESERVE, 0, 0);
    struct vm_page *b = uvm_pagealloc_strat(0, 0, 0, UVM_PGA_USERESERVE, 0, 0);
    struct vm_page *c = uvm_pagealloc_strat(0, 0, 0, UVM_PGA_USERESERVE, 0, 0);
    if (!a || !b || !c) { rvm_early_puts(rvm_cross_bad); rvm_boot_stop(); }
    paddr_t pa = VM_PAGE_TO_PHYS(a);
    volatile uint32_t *last = (volatile uint32_t *)(uintptr_t)(pa + RVM_MMU_PAGE_SIZE - 4);
    volatile uint32_t *middle = (volatile uint32_t *)(uintptr_t)VM_PAGE_TO_PHYS(b);
    volatile uint32_t *next = (volatile uint32_t *)(uintptr_t)VM_PAGE_TO_PHYS(c);
    pmap_kenter_pa(VM_KERNEL_VM_BASE, pa, VM_PROT_READ | VM_PROT_WRITE, 0);
    pmap_kenter_pa(VM_KERNEL_VM_BASE + RVM_MMU_PAGE_SIZE,
        VM_PAGE_TO_PHYS(c), VM_PROT_READ | VM_PROT_WRITE, 0);
    pmap_update(pmap_kernel());
    volatile uint64_t *cross = (volatile uint64_t *)(VM_KERNEL_VM_BASE + RVM_MMU_PAGE_SIZE - 4);
    *last = 0x11223344; *next = 0x55667788; *middle = 0x99aabbcc;
    if (*cross != 0x1122334455667788UL) {
        rvm_early_puts(rvm_cross_bad); rvm_boot_stop();
    }
    *cross = 0xfedcba9876543210UL;
    if (*last != 0xfedcba98 || *next != 0x76543210 || *middle != 0x99aabbcc) {
        rvm_early_puts(rvm_cross_bad); rvm_boot_stop();
    }
    pmap_kremove(VM_KERNEL_VM_BASE, 2 * RVM_MMU_PAGE_SIZE);
    pmap_update(pmap_kernel());
    uvm_pagefree(a); uvm_pagefree(b); uvm_pagefree(c);
    rvm_early_puts(rvm_cross_ok);
}
'''
        build_wrapper(boundary)
        out = run(work)
        assert 'MMU page boundary verified.' in out, out
        print('PASS: unaligned 64-bit reads/writes across virtual pages with '
              'noncontiguous physical backing', flush=True)

        build_wrapper((Path(__file__).parent / 'pmap.c').read_text())
        out = run(work)
        assert 'Kernel pmap operations verified.' in out, out
        print('PASS: kernel mapping replacement, leaf boundaries, statistics, protection, '
              'page zero/copy and sealed allocator', flush=True)

        build_wrapper((Path(__file__).parent / 'pagealloc.c').read_text())
        out = run(work)
        assert 'Managed UVM pages verified.' in out, out
        print('PASS: MI page metadata, allocation/free, zero-on-reuse, unique ownership, '
              'reserve exhaustion, restored counts and bootstrap locks', flush=True)

        build_wrapper((Path(__file__).parent / 'uvm.c').read_text(), preload=True)
        out = run(work)
        assert 'UVM preload and stealing verified.' in out, out
        assert ('UVM diagnostic RVM -7 4294967295 -9223372036854775808 '
                '18446744073709551615 -9223372036854775808 '
                '18446744073709551615') in out, out
        assert 'UVM hex fedcba98 fedcba9876543210 fedcba9876543210 %' in out, out
        print('PASS: real UVM segment ownership, contiguous/rounded stealing, '
              'zeroing, duplicate handoff rejection and variadic diagnostics', flush=True)

        for operation, expected in (
            ('uvm_pagealloc_strat((struct uvm_object *)1, 0, NULL, 0, 0, 0);',
             'Object/anonymous page services are not available'),
            ('uvm_pagealloc_strat(NULL, 0, (struct vm_anon *)1, 0, 0, 0);',
             'Object/anonymous page services are not available'),
            ('struct vm_page *pg = uvm_pagealloc_strat(NULL, 0, NULL, '
             'UVM_PGA_USERESERVE, 0, 0); uvm_pagefree(pg); uvm_pagefree(pg);',
             'Invalid raw UVM page operation'),
        ):
            body = '#include <uvm/uvm.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi);\n' + operation + '\n}\n'
            build_wrapper(body)
            out = run(work)
            assert ready in out and 'NetBSD/RVM UVM panic: ' + expected in out, out
            print('PASS: raw-page API rejection: ' + operation.split('(')[0], flush=True)

        for operation, expected in (
            ('uvm_page_physload(5, 5, 5, 5, VM_FREELIST_DEFAULT);',
             'NetBSD/RVM UVM panic: uvm_page_physload: start[5] >= end[5]'),
            ('uvm_page_physload(5, 6, 5, 6, VM_NFREELIST);',
             'NetBSD/RVM UVM panic: uvm_page_physload: bad free list 1'),
            ('pmap_steal_memory((vsize_t)-1, NULL, NULL);',
             'NetBSD/RVM UVM panic: Invalid UVM bootstrap allocation'),
            ('pmap_steal_memory((vsize_t)rvm_bootmem.ram_end, NULL, NULL);',
             'NetBSD/RVM UVM panic: No contiguous UVM bootstrap memory available'),
            ('uvm.page_init_done = true; pmap_steal_memory(PAGE_SIZE, NULL, NULL);',
             'NetBSD/RVM UVM panic: Invalid UVM bootstrap allocation'),
            ('kmutex_t lock; mutex_init(&lock, MUTEX_NODEBUG, IPL_VM); '
             'mutex_enter(&lock); mutex_enter(&lock);',
             'NetBSD/RVM UVM panic: Invalid bootstrap lock/IPL operation'),
        ):
            body = '#include <uvm/uvm.h>\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi);\n' + operation + '\n}\n'
            build_wrapper(body, preload=True)
            out = run(work)
            assert preload_ready in out and expected in out, out
            print('PASS: UVM rejection: ' + expected.split(': ')[-1], flush=True)

        for modification in ('version = 99', 'nsections = 17',
                             'section[0].size = ~0UL', 'loaded_end = 0',
                             'loader_stack = 0'):
            body = 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += '((struct rvm_bootinfo *)(uintptr_t)bi)->' + modification + ';\n'
            body += 'rvm_normal_early_init(bi); }\n'
            build_wrapper(body)
            out = run(work)
            assert 'Invalid loader memory handoff.' in out and ready not in out, out
            print('PASS: rejected boot record with ' + modification, flush=True)

        # All probes run after normal startup has enabled the hardware MMU.
        for name, pointer, assembly, vector in (
            ('null page', '0', ' move.q (r2),r0\n rts\n', 5),
            ('read-only text', '(uintptr_t)rvm_early_init', ' clr.q (r2)\n rts\n', 4),
            ('read-only data', '(uintptr_t)&kernel_pmap_ptr', ' clr.q (r2)\n rts\n', 4),
            ('non-executable data', 'RVM_BOOT_TEST_VA', ' jsr (r2)\n rts\n', 2),
        ):
            body = 'extern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); rvm_test_probe(' + pointer + '); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            assert ready in out, out
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection for ' + name, flush=True)

        # Access first, then change the PTE. This also detects stale translations.
        for name, operation, assembly, vector in (
            ('kernel write downgrade',
             'pmap_protect(pmap_kernel(), va, va + RVM_MMU_PAGE_SIZE, VM_PROT_READ);',
             ' clr.q (r2)\n rts\n', 4),
            ('kernel mapping removal', 'pmap_kremove(va, RVM_MMU_PAGE_SIZE);',
             ' move.q (r2),r0\n rts\n', 5),
            ('kernel PROT_NONE',
             'pmap_protect(pmap_kernel(), va, va + RVM_MMU_PAGE_SIZE, VM_PROT_NONE);',
             ' move.q (r2),r0\n rts\n', 5),
            ('kernel non-executable mapping', '', ' jsr (r2)\n rts\n', 2),
        ):
            body = 'extern void rvm_test_probe(uintptr_t);\n'
            body += 'void rvm_early_init(const struct rvm_bootinfo *bi) {\n'
            body += 'rvm_normal_early_init(bi); vaddr_t va = VM_KERNEL_VM_BASE;\n'
            body += ('pmap_kenter_pa(va, rvm_boot_pmap.test_page, '
                     'VM_PROT_READ | VM_PROT_WRITE, 0); pmap_update(pmap_kernel());\n')
            body += '*(volatile uint64_t *)va = 0;\n' + operation + '\n'
            body += 'pmap_update(pmap_kernel()); rvm_test_probe(va); }\n'
            build_wrapper(body, assembly)
            out = run(work)
            expected = f'vector 0x{vector:016x} version 0x0000000000000003'
            assert ready in out and expected in out and 'CPU stopped.' in out, out
            print('PASS: hardware protection after ' + name, flush=True)


if __name__ == '__main__':
    main()
