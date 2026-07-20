// Phase 9: NEON-accelerated hot loops. AAPCS64 calling convention -- both
// are leaf functions using only caller-saved registers (x0-x18, v0-v7,
// v16-v31), so no stack frame or register save/restore is needed. Each
// processes 4x 32-bit lanes per instruction (one 128-bit NEON register),
// with a scalar tail for any remainder. SoA's contiguous arrays (Phase 7)
// are exactly what makes the sequential load/store here possible --
// this would not vectorize cleanly over an array-of-structs.
.section ".text"
.global entity_update_positions_neon
.global neon_fill32

// void entity_update_positions_neon(int32_t *x, int32_t *y,
//                                    const int32_t *vx, const int32_t *vy,
//                                    uint32_t count)
// x0=x  x1=y  x2=vx  x3=vy  w4=count
entity_update_positions_neon:
1:
    cmp     w4, #4
    b.lt    2f
    ld1     {v0.4s}, [x0]
    ld1     {v1.4s}, [x2], #16
    add     v0.4s, v0.4s, v1.4s
    st1     {v0.4s}, [x0], #16

    ld1     {v2.4s}, [x1]
    ld1     {v3.4s}, [x3], #16
    add     v2.4s, v2.4s, v3.4s
    st1     {v2.4s}, [x1], #16

    sub     w4, w4, #4
    b       1b
2:
    cbz     w4, 4f
3:
    ldr     w5, [x0]
    ldr     w6, [x2], #4
    add     w5, w5, w6
    str     w5, [x0], #4

    ldr     w5, [x1]
    ldr     w6, [x3], #4
    add     w5, w5, w6
    str     w5, [x1], #4

    subs    w4, w4, #1
    b.ne    3b
4:
    ret

// void neon_fill32(uint32_t *dst, uint32_t value, uint32_t count)
// x0=dst  w1=value  w2=count
neon_fill32:
    dup     v0.4s, w1
1:
    cmp     w2, #4
    b.lt    2f
    st1     {v0.4s}, [x0], #16
    sub     w2, w2, #4
    b       1b
2:
    cbz     w2, 4f
3:
    str     w1, [x0], #4
    subs    w2, w2, #1
    b.ne    3b
4:
    ret
