/* GBA リンカスクリプト (devkitPro 非依存) */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    rom   : ORIGIN = 0x08000000, LENGTH = 32M
    iwram : ORIGIN = 0x03000000, LENGTH = 32K - 0x200
    ewram : ORIGIN = 0x02000000, LENGTH = 256K
}

SECTIONS {
    .text : {
        KEEP(*(.crt0))
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
    } > rom

    .rodata : ALIGN(4) {
        KEEP(*(.rodata.save_type_id))
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > rom

    .ARM.exidx : { *(.ARM.exidx*) } > rom

    __iwram_lma = ALIGN(4);
    .iwram : AT(__iwram_lma) ALIGN(4) {
        __iwram_start = .;
        *(.iwram .iwram.*)
        *(.data .data.*)
        . = ALIGN(4);
        __iwram_end = .;
    } > iwram

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > iwram

    /* EWRAM: 先頭にデバッグブロック(テストハーネスが読む)を固定配置 */
    .ewram_bss (NOLOAD) : ALIGN(4) {
        __ewram_bss_start = .;
        KEEP(*(.ewram_debug))
        *(.ewram_bss .ewram_bss.*)
        . = ALIGN(4);
        __ewram_bss_end = .;
    } > ewram

    __rom_end = __iwram_lma + SIZEOF(.iwram);

    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.attributes) }
}
