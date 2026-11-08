/* Linked after every program and library member. A real byte keeps this
 * symbol in FHR's data section even when the assembler drops empty sections.
 * The loader rounds the data extent to pages; sbrk starts at the same boundary.
 */
        .data
        .globl __rvm_end
__rvm_end:
        .byte 0
