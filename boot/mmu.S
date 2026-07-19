// Phase 3: Stage-1 identity-mapped translation tables. 4KB granule, 39-bit
// input address (T0SZ=25) so the walk starts at level 1 directly — RAM and
// the low device region are each naturally 1GB-aligned, so a single L1
// table of 1GB block descriptors covers everything with no L2/L3 tables.
//
//   l1_table[0] -> [0x00000000, 0x40000000) Device-nGnRnE (GIC/UART/fw_cfg)
//   l1_table[1] -> [0x40000000, 0x80000000) Normal WB, Inner Shareable (RAM)
//
// Everything else (l1_table[2..511]) is left zeroed by start.S's BSS clear
// — a zero descriptor has bit0 (valid) clear, so any stray access outside
// these two ranges takes a translation fault instead of silently aliasing.

.equ MAIR_DEVICE_NGNRNE, 0x00
.equ MAIR_NORMAL_WB,     0xff
.equ MAIR_VAL, (MAIR_NORMAL_WB << 8) | MAIR_DEVICE_NGNRNE

// Block descriptor bits (levels 0-2 block, not table: bits[1:0] = 0b01).
// AP bits[7:6] and NS bit[5] are left 0 (AP=00 = RW at EL1, no EL0 access).
.equ DESC_VALID_BLOCK, 0x1
.equ DESC_AF,          (1 << 10)
.equ DESC_ATTR0,       (0 << 2)      // AttrIndx = 0 (Device-nGnRnE)
.equ DESC_ATTR1,       (1 << 2)      // AttrIndx = 1 (Normal WB)
.equ DESC_SH_OUTER,    (2 << 8)      // device memory: Outer Shareable
.equ DESC_SH_INNER,    (3 << 8)      // normal cacheable memory: Inner Shareable
.equ DESC_PXN,         (1 << 53)
.equ DESC_UXN,         (1 << 54)

.equ DEVICE_DESC, (DESC_VALID_BLOCK | DESC_AF | DESC_ATTR0 | DESC_SH_OUTER | DESC_PXN | DESC_UXN)
.equ NORMAL_DESC,  (DESC_VALID_BLOCK | DESC_AF | DESC_ATTR1 | DESC_SH_INNER)

.section ".bss"
.align 12                    // translation tables must be 4KB-aligned
.global l1_table
l1_table:
    .space 4096               // 512 x 8-byte entries

.section ".text"
.global mmu_init
mmu_init:
    adrp    x0, l1_table
    add     x0, x0, :lo12:l1_table

    // DEVICE_DESC's PXN/UXN bits (53/54) put it outside both MOVZ's 16-bit-
    // shifted range and ORR's logical-immediate bitmask patterns, so build
    // it explicitly instead of via the `mov` pseudo-op.
    movz    x1, #(DEVICE_DESC & 0xffff)
    movk    x1, #((DEVICE_DESC >> 48) & 0xffff), lsl #48
    str     x1, [x0, #0]        // l1_table[0]: identity-map [0, 0x40000000)

    mov     x1, #NORMAL_DESC
    mov     x2, #0x40000000
    orr     x1, x1, x2
    str     x1, [x0, #8]        // l1_table[1]: identity-map [0x40000000, 0x80000000)

    // MAIR_EL1: index0 = Device-nGnRnE, index1 = Normal Write-Back.
    mov     x1, #MAIR_VAL
    msr     mair_el1, x1

    // TCR_EL1: 4KB granule (TG0=00, default), T0SZ=25 (39-bit IA), WBWA
    // table walks, Inner Shareable, IPS read from ID_AA64MMFR0_EL1.PARange
    // rather than assumed.
    mrs     x1, id_aa64mmfr0_el1
    and     x1, x1, #0x7               // PARange is a 3-bit field (values 0-6)
    mov     x2, #25                    // T0SZ, bits[5:0]
    orr     x2, x2, #(1 << 8)          // IRGN0 = 0b01 (WBWA)
    orr     x2, x2, #(1 << 10)         // ORGN0 = 0b01 (WBWA)
    orr     x2, x2, #(3 << 12)         // SH0 = 0b11 (Inner Shareable)
    lsl     x1, x1, #32                // IPS, bits[34:32]
    orr     x2, x2, x1
    msr     tcr_el1, x2

    msr     ttbr0_el1, x0

    isb
    tlbi    vmalle1
    dsb     ish
    isb

    // SCTLR_EL1: enable MMU (M), data cache (C), instruction cache (I).
    mrs     x1, sctlr_el1
    orr     x1, x1, #(1 << 0)          // M
    orr     x1, x1, #(1 << 2)          // C
    orr     x1, x1, #(1 << 12)         // I
    msr     sctlr_el1, x1
    isb

    ret
