# Silicon Swarm

A bare-metal (no OS, no libc) AArch64 game for QEMU `virt`: a Cities: Skylines-style
build phase feeding into a hundred-thousand-entity swarm siege phase, built for raw
cache/SIMD throughput rather than through an OS abstraction layer.

## Status

Pre-Phase-0. Toolchain is installed and verified (see below). No boot code yet —
next step is Phase 0 in the roadmap.

## Design decisions

- **Language:** freestanding C (`-ffreestanding -nostdlib`) for game logic and data
  structures. Hand-written AArch64 assembly for boot, exception vectors, MMU setup,
  context save/restore, and the hot entity-update/blit loop. NEON via intrinsics
  (`arm_neon.h`) or raw asm where cycle-level control matters.
- **Toolchain:** LLVM/Clang cross-compiling (`-target aarch64-none-elf`), `ld.lld`,
  `llvm-objcopy` — avoids building a separate GNU cross-toolchain from source on macOS.
- **Acceleration:** `-accel hvf -cpu host`. Host and guest are both AArch64 (Apple
  Silicon), so QEMU uses Apple's Hypervisor.framework instead of TCG software
  emulation. This matters a lot for the entity-count ceiling — confirmed working,
  see below.
- **GIC:** `-machine virt,gic-version=2`. GICv2 MMIO programming is far simpler than
  GICv3's system-register + redistributor model. GICv3 is a stretch goal.
- **Input (v1):** PL011 UART polling. A full `virtio-input` driver is a stretch phase.
- **Fixed-point math** (Q16.16 or similar) for positions/gradients in hot paths — no
  scalar float, no FPU/NEON mode-switch overhead.
- **SoA, not AoS.** Parallel flat arrays (`entity_x[]`, `entity_y[]`, `entity_hp[]`,
  ...), 64B-aligned, no per-entity structs.
- **No per-entity heap allocation, ever.** Static arrays sized to `MAX_ENTITIES` with
  an alive flag/freelist. A trivial bump allocator handles any scratch memory.
- **Spatial hashing**, not naive pairwise checks, for anything that scales with
  entity count squared.

## Host setup (macOS / Apple Silicon)

```bash
brew install llvm qemu lld dtc
```

Verified on this machine:
- `llvm 22.1.8`, `qemu 11.0.2`, `lld 22.1.8`, `dtc 1.8.1` — all installed.
- QEMU reports `hvf` in `qemu-system-aarch64 -accel help` — hardware acceleration
  will actually engage, not silently fall back to TCG.
- `clang -target aarch64-none-elf -ffreestanding -nostdlib` → `ld.lld` →
  `llvm-objcopy -O binary` pipeline compiles and links a trivial freestanding
  object end to end.

**Debugging:** use `lldb`, not `gdb`. Homebrew's `gdb` needs a self-signed
codesigning cert to attach to processes on macOS; `lldb` (bundled with Xcode
Command Line Tools) speaks the same gdbserver protocol QEMU's `-s -S` exposes,
so it works out of the box. See [`debug/lldbinit`](debug/lldbinit) —
`lldb -s debug/lldbinit` after `make debug` is running in another shell.

## Testing strategy

Most of `engine/` and `game/` (SoA entity storage, flow field, spatial hash) is
freestanding C with no hardware dependency — it doesn't need QEMU to be exercised.
Once those modules exist, compile and unit-test them natively on macOS with the
host's clang (a plain `-std=c11` build, no `-target aarch64-none-elf`) rather than
round-tripping through QEMU + UART for every logic change. Anything touching MMIO,
the MMU, or asm stays QEMU/UART-verified as described per-phase below.

## Hard constraints

- No operating system, no libc, no dynamic linking, no OS-provided heap allocator.
- Single flat binary loaded by QEMU via `-kernel`.
- Target: `qemu-system-aarch64 -machine virt -cpu host -accel hvf`.
- No floating point in hot paths unless NEON.
- Every subsystem independently testable via UART log output before being wired
  into the game loop.

## Project structure

```
silicon-swarm/
├── boot/
│   ├── start.S          # EL2->EL1 drop, sp_el1 init, BSS clear, jump to kmain
│   ├── vectors.S        # VBAR_EL1 exception vector table (16 entries)
│   └── mmu.S            # Stage-1 translation table setup, TTBR0_EL1, SCTLR_EL1
├── kernel/
│   ├── kmain.c          # Entry point after boot, subsystem init, main loop
│   ├── uart.c/.h        # PL011 driver (init, putc, getc/poll)
│   ├── timer.c/.h       # CNTP_TVAL_EL0 / CNTP_CTL_EL0, IRQ handler
│   ├── gic.c/.h         # GICv2 distributor + CPU interface init, IRQ enable/ack
│   ├── framebuffer.c/.h # fw_cfg + ramfb negotiation, pixel plotting API
│   └── alloc.c/.h       # bump allocator over a static arena
├── engine/
│   ├── entity_soa.c/.h  # struct-of-arrays entity storage (X[], Y[], HP[], type[])
│   ├── flowfield.c/.h   # grid gravity map, gradient generation, downhill lookup
│   ├── blit_neon.S      # NEON-accelerated pixel blit routines
│   └── spatial_hash.c/.h# grid-bucket collision/combat resolution
├── game/
│   ├── build_phase.c/.h # city grid state, zoning, road placement
│   ├── siege_phase.c/.h # spawn logic, wave scaling, win/loss conditions
│   └── input.c/.h       # UART key polling -> game action mapping
├── linker.ld
├── Makefile
└── debug/
    └── lldbinit          # lldb target + gdb-remote setup for QEMU's gdbstub
```

## Build system

- `make build` — compile + link → `silicon_swarm.elf`, `llvm-objcopy -O binary` →
  `silicon_swarm.img`
- `make run` — `qemu-system-aarch64 -M virt -cpu host -accel hvf -m 512 -nographic
  -kernel silicon_swarm.img` (UART on stdio for early phases)
- `make run-gfx` — same, with `-display cocoa` once ramfb works
- `make debug` — same as `run` plus `-s -S`, halts at reset for `lldb -s
  debug/lldbinit`
- `make dumpdtb` — dumps and decompiles the `virt` board's device tree, to verify
  MMIO addresses instead of trusting the table below blindly
- `make clean`

(None of these exist yet — added starting Phase 0.)

## QEMU `virt` memory map (verify with `make dumpdtb` — don't trust blindly)

| Device | Base Address | Notes |
|---|---|---|
| GIC Distributor (GICD) | `0x08000000` | GICv2 |
| GIC CPU Interface (GICC) | `0x08010000` | GICv2 |
| PL011 UART0 | `0x09000000` | IRQ 33 |
| PL031 RTC | `0x09010000` | not needed for v1 |
| fw_cfg | `0x09020000` | selector @ +0x08, data @ +0x00, DMA @ +0x10 |
| virtio-mmio bank | `0x0a000000` | 32 slots × `0x200` stride |
| RAM | `0x40000000` | `-kernel` load address |

Generic Timer (EL1 physical): non-secure PPI 14 → GIC interrupt ID 30. Confirm via
device tree dump — do not assume.

## Roadmap

Work through phases in order; each has a concrete "done when" test. Don't start a
phase before the previous one's test passes under `make run` — bare-metal bugs
compound silently, and an unverified lower layer produces baffling failures phases
later. Commit after each phase.

0. **Toolchain sanity** — boot a binary that spins (`b .`); confirm via `make
   debug` + lldb that PC sits in the loop.
1. **Boot + UART hello world** — EL2→EL1 drop (or detect already-EL1), BSS clear,
   PL011 polling `putc`. `"SILICON SWARM BOOT OK"` on every `make run`.
2. **Exception vectors** — full 16-entry `VBAR_EL1` table; default handlers print
   exception type + `ESR_EL1`/`ELR_EL1` and halt. A deliberate data abort produces
   a readable diagnostic, not a hang.
3. **MMU + caches** — identity-mapped Stage-1 tables, RAM as Normal/Cacheable, MMIO
   as Device-nGnRnE. UART still works with MMU on; measurable speedup on a tight
   loop with caches on vs. off.
4. **GIC + timer interrupt** — GICv2 init, IRQ 30 enabled, periodic `CNTP_TVAL_EL0`
   tick. A counter incremented only in the IRQ handler reaches ~60/sec against
   wall-clock.
5. **Framebuffer via ramfb** — negotiate `ramfb` through `fw_cfg` DMA (address,
   fourcc, width, height, stride). **Budget real iteration time here** — this is
   the fiddliest hardware-negotiation step in the project. `make run-gfx` shows a
   solid fill, then a computed test pattern.
6. **Input (UART polling)** — WASD + action keys move a test sprite.
7. **SoA entity storage + bump allocator** — 10,000 dummy entities moving in
   straight lines at a stable 60Hz.
8. **Flow field pathfinding** — wavefront gradient from city center; entities step
   downhill with no per-entity search; update cost stays flat as obstacle count
   grows.
9. **NEON blit + update loop** — vectorized position update / pixel write. Target
   is a stretch number (hundreds of thousands of entities at 60Hz), not a hard
   gate — Phase 7's flat-array layout is what makes this possible, but per-frame
   flowfield + spatial hash + blit cost on a single core may cap out lower.
10. **Spatial hashing** — grid-bucket collision/combat; two entity groups fight
    without frame time scaling quadratically with group size.
11. **Game loop integration** — `BUILD_PHASE` ↔ `SIEGE_PHASE` state machine,
    win/loss check, playable end to end.
12. **Performance pass** — `PMCCNTR_EL0` profiling (needs `PMUSERENR_EL0`), 64B
    cache-line alignment on SoA arrays, confirm HVF is actually engaged. Documented
    entity-count ceiling with the real bottleneck (bandwidth vs. compute vs.
    spatial hash overhead).

### Stretch goals (post-v1, do not start early)

- GICv3 (system-register interface, redistributors).
- Full `virtio-input` keyboard driver.
- Port to real Raspberry Pi 4 hardware (GPU mailbox instead of ramfb, real DTB).
- Multi-core via PSCI `CPU_ON`, with per-core cache-partition-aware SoA chunking.

## Working notes

- Comment every hardware register write with what the bits mean and why (e.g.
  `// SCTLR_EL1.M=1 (MMU enable), .C=1 (data cache), .I=1 (instr cache)`), not just
  the value — this code is unreadable without it.
- Prefer a loud stub over a silent skip: unimplemented subsystems print `"NOT YET
  IMPLEMENTED: <thing>"` over UART and halt.
- Verify before optimizing — get the scalar version correct and measured (Phase 7)
  before vectorizing (Phase 9), with a before/after comparison.
- If `make dumpdtb` disagrees with the memory map table above, trust the dump.
