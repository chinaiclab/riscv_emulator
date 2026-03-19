/* Custom linker script for MMU page fault test */
OUTPUT_FORMAT("elf32-littleriscv")
ENTRY(_start)

/* Memory layout */
MEMORY
{
  rom (rx) : ORIGIN = 0x00000000, LENGTH = 0x00080000  /* 512KB */
  ram (rwx) : ORIGIN = 0x00080000, LENGTH = 0x00080000  /* 512KB */
  virt_code (rwx) : ORIGIN = 0x14000, LENGTH = 0x1000   /* 4KB at virtual address 0x14000 */
}

SECTIONS
{
  /* Place code section at the beginning of ROM */
  .text : {
    *(.text)
    *(.text.*)
    . = ALIGN(4);
  } > rom

  /* Read-only data section */
  .rodata : {
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(4);
  } > rom

  /* Special section for continuation code at virtual address 0x14000 */
  .virt_text : {
    *(.virt_text)
    *(.virt_text.*)
    . = ALIGN(4);
  } > virt_code

  /* Initialized data section */
  .data : {
    *(.data)
    *(.data.*)
    . = ALIGN(4);
  } > ram

  /* Zero-initialized data section */
  .bss : {
    *(.bss)
    *(.bss.*)
    *(.COMMON)
    . = ALIGN(4);
  } > ram

  end = .;
}