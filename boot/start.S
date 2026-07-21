// Phase 1: EL2->EL1 drop (or detect already-EL1), stack + BSS init, jump to
// kmain. QEMU's `virt` machine can reset either at EL2 or straight at EL1
// depending on version/config, so handle both rather than assuming.
.section ".text.boot"
.global _start

.equ SPSR_EL1h_MASKED, 0x3c5   // M[3:0]=0101 (EL1h), D=A=I=F=1 (all masked)

_start:
    mrs     x0, CurrentEL
    and     x0, x0, #0xc            // CurrentEL.EL is bits [3:2]
    cmp     x0, #0x8                // 0b1000 == EL2
    b.eq    in_el2
    cmp     x0, #0x4                // 0b0100 == EL1
    b.eq    in_el1
    b       .                       // EL0/EL3: unsupported, hang loudly enough

in_el2:
    // HCR_EL2.RW=1: EL1/EL0 execute in AArch64 state, not AArch32.
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    // CPTR_EL2: don't trap FP/SIMD/SVE accesses made from EL1 (0x33ff clears
    // the TFP/TZ trap bits) — Phase 9's NEON code needs this to already be
    // set correctly rather than debugging a trap-to-EL2 three phases later.
    mov     x0, #0x33ff
    msr     cptr_el2, x0
    msr     hstr_el2, xzr

    // CNTHCTL_EL2: let EL1 access the physical generic timer/counter
    // directly (needed for Phase 4), with no virtual offset.
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    // MDCR_EL2 = 0: clears TPM/TPMCR (trap performance-monitor register
    // access from EL1 to EL2) among everything else -- Phase 12 needs
    // EL1 to read PMCCNTR_EL0 directly. We never install an EL2 vector
    // table (we drop to EL1 permanently right after this), so leaving
    // any trap bit set here would fault with nothing to handle it.
    msr     mdcr_el2, xzr

    // Land at EL1h (dedicated SP_EL1) with all exceptions masked — Phase 2
    // installs real vectors and unmasks them deliberately.
    mov     x0, #SPSR_EL1h_MASKED
    msr     spsr_el2, x0

    adrp    x0, stack_top
    add     x0, x0, :lo12:stack_top
    msr     sp_el1, x0

    adr     x0, in_el1
    msr     elr_el2, x0
    eret

in_el1:
    // Reached either via eret from EL2, or because QEMU reset straight into
    // EL1 (observed on some machine configs) — in the latter case SP_EL1
    // hasn't been set, so (re)point SP at the boot stack unconditionally.
    msr     spsel, #1
    isb
    adrp    x0, stack_top
    add     x0, x0, :lo12:stack_top
    mov     sp, x0

    // Zero .bss (__bss_start/__bss_end come from linker.ld).
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:
    cmp     x0, x1
    b.ge    2f
    str     xzr, [x0], #8
    b       1b
2:
    // CPACR_EL1.FPEN=0b11: EL0/EL1 FP/SIMD access untrapped. Found the hard
    // way in Phase 5 — even "plain C" code needs this, since -O2 silently
    // auto-vectorizes eligible loops (e.g. the framebuffer fill) into NEON
    // instructions, which trap with EC=7 otherwise. Doing this before
    // kmain, not just before Phase 9's deliberate NEON code, for exactly
    // that reason.
    mov     x0, #(0x3 << 20)
    msr     cpacr_el1, x0
    isb

    bl      kmain
    b       .                       // kmain must never return

.section ".bss"
.align 4
stack_bottom:
    .space 65536
stack_top:
