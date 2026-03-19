/* Simple linker script for RV32 bare-metal multi-core program */
OUTPUT_FORMAT("elf32-littleriscv")
ENTRY(_start)

/* Memory layout */
MEMORY
{
  /* ROM region for code and read-only data */
  rom (rx) : ORIGIN = 0x00000000, LENGTH = 0x00080000  /* 512KB ROM */

  /* RAM region for data */
  ram (rwx) : ORIGIN = 0x00080000, LENGTH = 0x00080000  /* 512KB RAM */

  /* Memory-mapped I/O region */
  mmio (rwx) : ORIGIN = 0x00090000, LENGTH = 0x00010000  /* 64KB MMIO */
}

/* Core identifier definitions */
__CORE_ID__ = 0;
__CORE_COUNT__ = 4;

SECTIONS
{
  /* Code section - placed at the beginning of ROM */
  .text : {
    KEEP(*(.core0_boot))      /* Core 0 startup code */
    *(.text.start)            /* Startup initialization code */
    *(.text.main)             /* Main program code */
    *(.text)                  /* All other text sections */
    *(.text.*)
    . = ALIGN(4);
  } > rom

  /* Read-only data section */
  .rodata : {
    *(.rodata)
    *(.rodata.*)
    . = ALIGN(4);
  } > rom

  /* Initialized data section in RAM */
  .data : {
    *(.data)
    *(.data.*)
    . = ALIGN(4);
  } > ram

  /* BSS section in RAM */
  .bss : {
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(8);
  } > ram

  /* Stack sections - one per core to prevent conflicts */
  .stack_core0 : {
    . = ALIGN(16);
    . = . + 0x2000;  /* 8KB stack for core 0 */
  } > ram

  .stack_core1 : {
    . = ALIGN(16);
    . = . + 0x2000;  /* 8KB stack for core 1 */
  } > ram

  .stack_core2 : {
    . = ALIGN(16);
    . = . + 0x2000;  /* 8KB stack for core 2 */
  } > ram

  .stack_core3 : {
    . = ALIGN(16);
    . = . + 0x2000;  /* 8KB stack for core 3 */
  } > ram

  end = .;
}

/* Core-specific stack top symbols for runtime */
__stack_top_core0 = ADDR(.stack_core0) + SIZEOF(.stack_core0);
__stack_top_core1 = ADDR(.stack_core1) + SIZEOF(.stack_core1);
__stack_top_core2 = ADDR(.stack_core2) + SIZEOF(.stack_core2);
__stack_top_core3 = ADDR(.stack_core3) + SIZEOF(.stack_core3);

/* Main program start symbol */
__main_program_start = ADDR(.text);

/* Provide errno symbol for retarget.c */
PROVIDE(__errno = .);