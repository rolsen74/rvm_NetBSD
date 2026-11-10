/* Static RVM section-base ABI. Startup loads these relocated addresses.
 * Keep empty bases backed by a section so the FHR loader can relocate them. */
ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : { *(.text .text.*) }
  . = ALIGN(16);
  .data : { __rvm_data_base = .; *(.data .data.*) . += 8; }
  . = ALIGN(16);
  .rodata : { __rvm_rodata_base = .; *(.rodata .rodata.*) . += 8; }
  . = ALIGN(16);
  .bss (NOLOAD) : { __rvm_bss_base = .; *(.bss .bss.*) *(COMMON) . += 8; *(.rvm_end) }
}
