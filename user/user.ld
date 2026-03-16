ENTRY(_start)

SECTIONS
{
  . = 0x400100;

  .text : {
    *(.text*)
  }

  .rodata : {
    *(.rodata*)
  }

  .data : {
    *(.data*)
  }

  .bss : {
    *(.bss*)
    *(COMMON)
  }

  .init_array : {
    __init_array_start = .;
    KEEP(*(.init_array*))
    __init_array_end = .;
  }

  .fini_array : {
    __fini_array_start = .;
    KEEP(*(.fini_array*))
    __fini_array_end = .;
  }
}
