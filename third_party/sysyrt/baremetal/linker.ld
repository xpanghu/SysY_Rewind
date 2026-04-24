OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

MEMORY
{
  RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS
{
  . = ORIGIN(RAM);

  .text : ALIGN(4) {
    KEEP(*(.text.start))
    *(.text .text.*)
    *(.rodata .rodata.*)
  } > RAM :text

  .data : ALIGN(4) {
    *(.data .data.*)
  } > RAM :data

  .bss (NOLOAD) : ALIGN(4) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } > RAM :data

  . = ALIGN(16);
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
