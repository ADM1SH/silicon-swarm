// Phase 0: minimal reset handler. Just spin — proves the toolchain and
// QEMU -kernel load path work before any real boot logic exists.
.section ".text.boot"
.global _start
_start:
    b .
