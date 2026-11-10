/* Static FHR entry. Stack: MI argc, argv[], NULL, envp[], NULL.
 * R5 supplies the MI ps_strings pointer and remains the fourth C argument. */
        .text
        .globl _start
        .globl __rvm_section_bases_v1
__rvm_section_bases_v1:
_start:
        /* Absolute relocated LEAs bootstrap the section bases themselves. */
        lea __rvm_rodata_base,r50
        lea __rvm_data_base,r51
        lea __rvm_bss_base,r52
        move.q (r63),r2
        lea (8,r63),r3
        move.q r2,r4
        add.q #2,r4
        lsl.q #3,r4
        add.q r63,r4
        sub.q #80,r63
        jsr __rvm_start
stop:
        jmp stop
