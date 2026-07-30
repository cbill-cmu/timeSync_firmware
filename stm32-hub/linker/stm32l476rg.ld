/* Linker script for STM32L476RG (Nucleo-64): 1024K flash, 96K RAM.
 *
 * DIAGNOSTIC: intentionally using only SRAM1 (96K @ 0x20000000-0x20017FFF)
 * here, NOT the full 128K (SRAM1 + SRAM2 aliased at 0x20018000-0x2001FFFF).
 * Every HardFault seen in bring-up testing was a BusFault STKERR (fault
 * while stacking an exception frame) with msp landing in the SRAM2-alias
 * region, reproducible across three different physical boards -- testing
 * whether avoiding that region entirely fixes it. If this works, either
 * SRAM2 needs some enable step this project doesn't do yet, or its alias
 * behaves differently than plain SRAM1 on this part; revisit before
 * needing the extra 32K.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
