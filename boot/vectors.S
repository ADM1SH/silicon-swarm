// Phase 2: full 16-entry VBAR_EL1 exception vector table.
//
// We run entirely at EL1h (dedicated SP_EL1) once start.S hands off to
// kmain, so in practice only the four "Current EL with SPx" entries are
// ever taken (offset group 0x200-0x380) — Sync for things like data aborts,
// IRQ for the Phase 4 timer tick. The other twelve entries exist because
// the architecture requires a full table, and each still saves context and
// reports a readable diagnostic instead of silently hanging if ever hit.
//
// Each entry is a fixed 0x80-byte slot, too small to inline the full
// register save (that alone is ~20 instructions = 80 bytes, plus the C call
// and restore). So each slot does the minimum: allocate the trap frame,
// stash the two registers we need as scratch (x0 = exception type id, x1 =
// temp) directly into their final frame slots, then branch to the shared
// out-of-line handler that does the rest. This keeps every slot under the
// 0x80 budget regardless of vector count.

#define CTX_SIZE 272   // 31 GPRs (x0-x30) + ELR_EL1 + SPSR_EL1, padded to 16B

.macro VECTOR_ENTRY type
    .balign 0x80
    sub     sp, sp, #CTX_SIZE
    stp     x0, x1, [sp, #0]      // real x0/x1 saved before we clobber x0
    mov     x0, #\type
    b       exc_common
.endm

.section ".text.vectors"
.balign 2048                       // VBAR_EL1 requires 2^11 alignment
.global vectors
vectors:
    // Current EL with SP0 (never used — we're always on SP_EL1 by the time
    // vectors are live) — offsets 0x000-0x180
    VECTOR_ENTRY 0                 // Synchronous
    VECTOR_ENTRY 1                 // IRQ
    VECTOR_ENTRY 2                 // FIQ
    VECTOR_ENTRY 3                 // SError

    // Current EL with SPx (the real, expected path) — offsets 0x200-0x380
    VECTOR_ENTRY 4                 // Synchronous
    VECTOR_ENTRY 5                 // IRQ
    VECTOR_ENTRY 6                 // FIQ
    VECTOR_ENTRY 7                 // SError

    // Lower EL, AArch64 (never used — nothing runs below EL1 in this
    // project) — offsets 0x400-0x580
    VECTOR_ENTRY 8                 // Synchronous
    VECTOR_ENTRY 9                 // IRQ
    VECTOR_ENTRY 10                // FIQ
    VECTOR_ENTRY 11                // SError

    // Lower EL, AArch32 (never used) — offsets 0x600-0x780
    VECTOR_ENTRY 12                // Synchronous
    VECTOR_ENTRY 13                // IRQ
    VECTOR_ENTRY 14                // FIQ
    VECTOR_ENTRY 15                // SError

// Shared handler body. On entry: x0 = exception type id, sp = trap frame
// base with real x0/x1 already stored at offsets 0/8, x2-x30/ELR/SPSR not
// yet saved.
exc_common:
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    mrs     x2, elr_el1
    mrs     x3, spsr_el1
    stp     x30, x2,  [sp, #240]
    str     x3,       [sp, #256]

    mrs     x1, esr_el1             // arg2: esr
    mov     x2, sp                  // arg3: trap_frame*
    // x0 already holds arg1: exception type id
    bl      exc_dispatch

    // exc_dispatch only returns for the serviced-IRQ path (Phase 4+); every
    // other path halts internally and never gets here.
    ldr     x3,       [sp, #256]
    ldp     x30, x2,  [sp, #240]
    msr     spsr_el1, x3
    msr     elr_el1,  x2
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8,  x9,  [sp, #64]
    ldp     x6,  x7,  [sp, #48]
    ldp     x4,  x5,  [sp, #32]
    ldp     x2,  x3,  [sp, #16]
    ldp     x0,  x1,  [sp, #0]
    add     sp, sp, #CTX_SIZE
    eret
