# Silicon Swarm

A bare-metal (no OS, no libc) AArch64 game for QEMU `virt`: an isometric
RollerCoaster-Tycoon-style city builder feeding into an entity-swarm siege,
built for raw cache/SIMD throughput rather than through an OS abstraction
layer.

## Status

**v2 complete** — isometric "3D" at 1280×720 on top of the finished v1 engine
(all 12 v1 roadmap phases plus v2 Phases 0–6). The world is a 128×128
heightmap rendered RCT-style (2:1 diamond projection, corner heights, the
RCT ±1 slope rule, painter's-order flat-shaded quads, every span filled by
NEON assembly). You terraform, build a road/house economy, wall the core
with barricades and turrets, then hold off a 2,000-entity siege pathed by a
flow field and resolved through spatial-hash combat. HUD, win bounty, and
full game-over/restart loop included. Profiled with `PMCCNTR_EL0`: worst
observed frame ≈ 2.5M cycles against a ≥16M-cycle 60Hz budget.

## How to play

```bash
./play.command
```

Double-click it in Finder, or run it from a terminal — it's `make build &&
make run-gfx`, opening the game in its own window.

> **Type into the terminal window, not the game window.** Input reaches the
> game over the serial port (`-serial stdio`), so keystrokes go into the
> terminal that launched QEMU — the one printing the boot log — while the
> graphical window just displays the game. Keys pressed in the graphical
> window go nowhere (there is no virtual keyboard device).

**Build phase** (starts immediately, yellow tile = cursor, tall green prism
= your city core on the central plateau):
- WASD — move the cursor one tile; the camera follows
- `q` / `e` — raise / lower terrain under the cursor (RCT terraforming;
  buildings need flat ground, so sculpt first)
- `1` barricade $10 (blocks pathing) · `2` turret $50 (shoots, 2.5-tile
  range) · `3` road $5 · `4` house $20 (earns $2/s **only while touching a
  road**)
- Space — place · `x` — demolish (half refund) · Enter — start the siege

**Siege phase**: automatic. 2,000 attackers (blue) spawn at the world edges
and flow-field their way to your core, routing around buildings and getting
shot by turret defenders (gold) on the way. Win (all attackers dead) pays a
$150 bounty and returns to building; lose (core HP 0) shows the game-over
banner — Enter starts a fresh map. HUD top-left shows money, tool or core
HP/foe count.

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

Most of `engine/` and `game/build_phase.c` (SoA entity storage, flow field,
spatial hash, city grid) is freestanding C with no hardware dependency, so it's
unit-tested natively on macOS with the host's own clang (`make test-host`, a
plain `-std=c11` build, no `-target aarch64-none-elf`, no QEMU) instead of
round-tripping through UART for every logic change:
`tests/test_entity_soa.c`, `test_alloc.c`, `test_flowfield.c`,
`test_spatial_hash.c`, `test_build_phase.c`. Anything touching MMIO, the MMU,
or asm — `game/siege_phase.c`'s per-tick loop included, since it drives the
NEON update primitive — stays QEMU/UART-verified as described per-phase below.

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
│   ├── alloc.c/.h       # bump allocator over a static arena
│   └── perf.c/.h        # PMCCNTR_EL0 cycle counter (Phase 12)
├── engine/
│   ├── entity_soa.c/.h  # struct-of-arrays entity storage (X[], Y[], HP[], type[])
│   ├── flowfield.c/.h   # grid gravity map, gradient generation, downhill lookup
│   ├── blit_neon.S/.h   # NEON-accelerated position update + framebuffer fill
│   └── spatial_hash.c/.h# grid-bucket collision/combat resolution
├── game/
│   ├── build_phase.c/.h # city grid state, tile placement (barricade/turret)
│   ├── siege_phase.c/.h # wave spawn, per-tick simulation, win/loss (single
│   │                     # wave, no escalation -- v1 minimal-loop scope)
│   └── input.c/.h       # UART key polling -> game action mapping
├── tests/                # host-side unit tests, run via `make test-host`
├── linker.ld
├── Makefile
├── play.command          # double-click launcher (make build && make run-gfx)
└── debug/
    └── lldbinit          # lldb target + gdb-remote setup for QEMU's gdbstub
```

## Build system

- `make build` — compile + link → `silicon_swarm.elf`, `llvm-objcopy -O binary` →
  `silicon_swarm.img`
- `make run` — `qemu-system-aarch64 -M virt -cpu host -accel hvf -m 512 -nographic
  -kernel silicon_swarm.img` (UART on stdio, no window — useful for reading the
  tick/combat/cycle log directly)
- `make run-gfx` — same, with `-device ramfb -display cocoa` for the actual game
  window
- `make debug` — same as `run` plus `-s -S`, halts at reset for `lldb -s
  debug/lldbinit`
- `make dumpdtb` — dumps and decompiles the `virt` board's device tree, to verify
  MMIO addresses instead of trusting the table below blindly
- `make test-host` — builds and runs the host-side unit tests (see Testing
  strategy) with the host's own clang, no QEMU involved
- `make clean`

`./play.command` wraps `make build && make run-gfx` for double-click launching
from Finder — see "How to play" above.

## QEMU `virt` memory map (verify with `make dumpdtb` — don't trust blindly)

| Device | Base Address | Notes |
|---|---|---|
| GIC Distributor (GICD) | `0x08000000` | GICv2 |
| GIC CPU Interface (GICC) | `0x08010000` | GICv2 |
| PL011 UART0 | `0x09000000` | IRQ 33 |
| PL031 RTC | `0x09010000` | not needed for v1 |
| fw_cfg | `0x09020000` | selector @ +0x08, data @ +0x00, DMA @ +0x10 |
| virtio-mmio bank | `0x0a000000` | 32 slots × `0x200` stride |
| RAM | `0x40000000` | RAM base. Our code links/loads at `0x40080000` — see below. |

Generic Timer, EL1 physical: non-secure PPI 14 → GIC interrupt ID 30. EL1
virtual: PPI 11 → GIC interrupt ID 27. Both confirmed via `make dumpdtb`.
**We use the virtual timer, not the physical one** — see Phase 4 in the
roadmap below for why (a QEMU 11.0.2 `-cpu host`+hvf-specific bug in the
physical timer's compare-value write path).

**Verified the hard way in Phase 2:** for a raw/flat `-kernel` image (no ELF
header, no Linux Image magic — exactly what `llvm-objcopy -O binary` produces),
QEMU's `arm_load_kernel()` has nothing to read an entry point from, so it falls
back to the AArch64 Linux boot protocol convention: install a tiny synthetic
stub at RAM base (`0x40000000`) that sets `x1`-`x3` to 0 and jumps to
`RAM_base + 0x80000`, and load the actual kernel image bytes there instead of
at RAM base. `linker.ld` links everything starting at `0x40080000` to match.
Getting this wrong doesn't stop the board from booting — PC-relative code
(branches, `adr`, `bl`) keeps working since it's self-consistent regardless of
an overall base offset — it silently corrupts every *absolute* address a
mismatched link computes (stack pointer, `VBAR_EL1`, saved `ELR_EL1`, any
string/jump table lookup), which is exactly why Phase 0's spin-loop and Phase
1's UART-only boot message both looked fine while linked 0x80000 too low, and
only Phase 2's exception diagnostics (the first genuinely absolute-address-
dependent code) exposed it.

## Roadmap

**All 12 phases complete** — commit history has one commit per phase. Kept
below as the design record of what each phase built and its concrete "done
when" test, not a forward-looking TODO list.

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
