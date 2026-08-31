/*
 * Espressif's bus-mirror ELF loader concatenates loaded data sections without
 * preserving their ELF virtual-address gaps. Round .rodata up here so the
 * following .data.rel.ro section (C++ vtables) remains four-byte aligned.
 */
SECTIONS
{
  .rodata ALIGN(4) :
  {
    *(.rodata .rodata.*)
    . = ALIGN(4);
  }
}
INSERT AFTER .text;
