ENTRY(_start)

SECTIONS
{
    /* RAM base is 0x40000000 (README §5), but QEMU's arm_load_kernel() has
     * no ELF/Image header to read an entry point from for a raw objcopy'd
     * binary, so it falls back to the AArch64 Linux boot protocol's
     * conventional TEXT_OFFSET and actually loads+jumps to base+0x80000,
     * with a synthetic stub at 0x40000000 doing the jump. Verified by
     * disassembling live guest memory over the lldb/gdbstub connection —
     * our own code was found sitting at 0x40080000, not 0x40000000, and a
     * linker.ld mismatched against that produced correct-looking PC-relative
     * codegen that nonetheless resolved every adrp/adr-derived absolute
     * address 0x80000 too low at runtime (stack pointer, VBAR_EL1 target,
     * saved ELR_EL1, exc_name() string-table lookups all silently wrong).
     */
    . = 0x40080000;

    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    __bss_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
