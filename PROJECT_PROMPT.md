# Silicon Swarm — Bare-Metal AArch64 Game Engine
### Full Project Prompt for Claude Code

**One-line pitch:** A bare-metal (no OS, no libc) AArch64 game for QEMU `virt`,
combining a Cities: Skylines-style build phase with a hundred-thousand-entity
swarm siege phase, built for raw cache/SIMD throughput.

This is the complete, standalone prompt for this project — everything needed to
pick it up in a fresh session with no prior context. It supersedes the original
brief: three environment gaps identified in review (debugger, ramfb risk, native
testing) are now resolved and folded in below. Repo:
https://github.com/ADM1SH/silicon-swarm.

---

## 0. Read This First — Decisions Made On Your Behalf

Explicit defaults so you don't have to make architecture calls mid-implementation.
Override any of these if you disagree, but state the tradeoff before you do.

1. **Language policy:** C (freestanding, `-ffreestanding -nostdlib`, no libc) for
   game logic, state machines, and data structures. Hand-written AArch64 assembly
   is mandatory for: the boot sequence, exception vector table, MMU/translation
   table setup, context save/restore, and the hot entity-update/blit loop. NEON
   may be accessed via either inline asm or GCC/Clang ARM NEON intrinsics
   (`arm_neon.h`) — intrinsics are fine and still compile to the same
   instructions; use raw asm where you need cycle-level control.
2. **Toolchain:** Native LLVM/Clang cross-compilation
   (`-target aarch64-none-elf`), not a separate GNU cross-toolchain build. Link
   with `ld.lld`. Convert with `llvm-objcopy`. This avoids building
   `binutils`/`gcc` from source on macOS. Installed via
   `brew install llvm qemu lld dtc` — versions verified on the dev machine:
   `llvm 22.1.8`, `qemu 11.0.2`, `lld 22.1.8`, `dtc 1.8.1`.
3. **QEMU acceleration:** Use `-accel hvf -cpu host`. Host and guest are both
   AArch64 (Apple Silicon host), so QEMU can use Apple's Hypervisor.framework
   instead of TCG software emulation. **This matters a lot** — TCG emulation
   would tank your entity-count ceiling and undermine the entire point of the
   project. `hvf` is confirmed present in `qemu-system-aarch64 -accel help` on
   the dev machine — do not skip re-confirming this on any new machine before
   doing performance work.
4. **GIC version:** `-machine virt,gic-version=2`. GICv2 MMIO programming is
   dramatically simpler than GICv3's system-register + redistributor model.
   Treat GICv3 as a stretch goal, not a v1 requirement.
5. **Input:** PL011 UART polling for v1 (discrete keypresses — WASD to pan,
   click-equivalent keys to place buildings, space to trigger siege phase). A
   full `virtio-input` keyboard driver (device discovery over `virtio-mmio`,
   virtqueue setup, descriptor/avail/used rings) is real driver work and is a
   **stretch phase**, not a blocker for playability.
6. **Debugger:** `lldb`, not `gdb`. Homebrew's `gdb` needs a self-signed
   codesigning cert to attach to processes on macOS — a genuine setup gap if
   left unaddressed. `lldb` (bundled with Xcode Command Line Tools) speaks the
   same gdbserver protocol QEMU's `-s -S` flag exposes, so it works without any
   extra install or codesigning. Use `debug/lldbinit`
   (`target create build/silicon_swarm.elf` + `gdb-remote localhost:1234`) with
   `lldb -s debug/lldbinit` after `make debug` is running in another shell. Do
   not introduce `gdb` into this project.
7. **Testing strategy:** Most of `engine/` and `game/` (SoA entity storage, flow
   field, spatial hash) is freestanding C with no hardware dependency — it does
   not need QEMU to be exercised. As soon as the first such module exists,
   compile and unit-test it natively on macOS with the host's own clang (a plain
   `-std=c11` build, no `-target aarch64-none-elf`, no linking against the
   AArch64 image) instead of always round-tripping through QEMU + UART for every
   logic change. Anything touching MMIO, the MMU, exception handling, or asm
   stays QEMU/UART-verified per the phase gates below — do not try to
   host-test those. Add a `make test-host` target once the first testable
   module exists; don't scaffold it before there's anything to test.

---

## 1. Hard Constraints

- No operating system. No libc. No dynamic linking. No heap allocator from an OS
  (you'll write a trivial bump allocator over a static arena).
- Everything must run from a single flat binary loaded by QEMU via `-kernel`.
- Target: `qemu-system-aarch64 -machine virt -cpu host -accel hvf`.
- No floating point in hot paths unless NEON — prefer fixed-point (Q16.16 or
  similar) for flow field gradients and entity positions to avoid FPU/NEON
  mode-switch overhead.
- Every subsystem must be independently testable via serial (UART) log output
  before it's wired into the game loop.

---

## 2. Host Setup (macOS / Apple Silicon)

```bash
brew install llvm qemu lld dtc
```

`dtc` (device tree compiler) is for dumping and reading the `virt` board's
device tree — you will need this to confirm MMIO addresses rather than trusting
hardcoded values blindly (see §5).

No `gdb` install needed — see debugger decision in §0.6.

Sanity-check the pipeline before writing any project code:

```bash
echo 'void _start(void){ while(1); }' > /tmp/check.c
clang -target aarch64-none-elf -ffreestanding -nostdlib -c /tmp/check.c -o /tmp/check.o
ld.lld -o /tmp/check.elf /tmp/check.o --entry=_start -Ttext=0x40000000
llvm-objcopy -O binary /tmp/check.elf /tmp/check.img
qemu-system-aarch64 -accel help   # must list "hvf"
```

If any of those fail, fix the environment before starting Phase 0 — a broken
toolchain produces baffling failures once real boot code is involved.

---

## 3. Project Structure

```
silicon-swarm/
├── boot/
│   ├── start.S          # EL2->EL1 drop, sp_el1 init, BSS clear, jump to kmain
│   ├── vectors.S         # VBAR_EL1 exception vector table (16 entries)
│   └── mmu.S             # Stage-1 translation table setup, TTBR0_EL1, SCTLR_EL1
├── kernel/
│   ├── kmain.c            # Entry point after boot, subsystem init, main loop
│   ├── uart.c/.h          # PL011 driver (init, putc, getc/poll)
│   ├── timer.c/.h         # CNTP_TVAL_EL0 / CNTP_CTL_EL0, IRQ handler
│   ├── gic.c/.h           # GICv2 distributor + CPU interface init, IRQ enable/ack
│   ├── framebuffer.c/.h   # fw_cfg + ramfb negotiation, pixel plotting API
│   └── alloc.c/.h         # bump allocator over a static arena
├── engine/
│   ├── entity_soa.c/.h    # struct-of-arrays entity storage (X[], Y[], HP[], type[])
│   ├── flowfield.c/.h     # grid gravity map, gradient generation, downhill lookup
│   ├── blit_neon.S         # NEON-accelerated pixel blit routines
│   └── spatial_hash.c/.h  # grid-bucket collision/combat resolution
├── game/
│   ├── build_phase.c/.h   # city grid state, zoning, road placement
│   ├── siege_phase.c/.h   # spawn logic, wave scaling, win/loss conditions
│   └── input.c/.h         # UART key polling -> game action mapping
├── linker.ld
├── Makefile
├── README.md
├── handoff.md
└── debug/
    └── lldbinit            # lldb target + gdb-remote setup (see §0.6)
```

`engine/` and `game/` modules should be written to compile standalone with a
host compiler wherever they don't touch MMIO or asm, per the testing strategy
in §0.7 — keep hardware-touching code isolated in `kernel/` and the `boot/` asm
files so the bulk of `engine/`/`game/` stays host-testable.

---

## 4. Build System

`Makefile` targets required:

- `make build` — compile + link → `silicon_swarm.elf`, then
  `llvm-objcopy -O binary` → `silicon_swarm.img`
- `make run` — `qemu-system-aarch64 -M virt -cpu host -accel hvf -m 512
  -nographic -kernel silicon_swarm.img` (nographic = UART on stdio for early
  phases)
- `make run-gfx` — same but with `-display cocoa` (or SDL) once ramfb is
  working, for actual visual output
- `make debug` — same as `run` plus `-s -S`, halts at reset waiting for a
  debugger to attach (`lldb -s debug/lldbinit` — see §0.6, not `gdb`)
- `make dumpdtb` — `qemu-system-aarch64 -M virt,dumpdtb=virt.dtb -cpu host` then
  `dtc -I dtb -O dts virt.dtb` — use this to **verify** MMIO addresses below
  rather than trusting them blindly
- `make test-host` — added once the first hardware-independent `engine/`/`game/`
  module exists (§0.7); compiles and runs that module's unit tests with the host
  compiler, no QEMU involved
- `make clean`

---

## 5. QEMU `virt` Reference Memory Map

These addresses have been stable across QEMU versions for the `virt` machine
type, but **verify with `make dumpdtb` before hardcoding anything** — don't take
this table on faith:

| Device | Base Address | Notes |
|---|---|---|
| GIC Distributor (GICD) | `0x08000000` | GICv2 |
| GIC CPU Interface (GICC) | `0x08010000` | GICv2 |
| PL011 UART0 | `0x09000000` | IRQ 33 |
| PL031 RTC | `0x09010000` | not needed for v1 |
| fw_cfg | `0x09020000` | selector @ +0x08, data @ +0x00, DMA @ +0x10 |
| virtio-mmio bank | `0x0a000000` | 32 slots × `0x200` stride |
| RAM | `0x40000000` | `-kernel` load address |

Generic Timer (EL1 physical): non-secure PPI 14 → GIC interrupt ID **30**.
Confirm via device tree dump — do not assume.

---

## 6. Phased Implementation Roadmap

Work through these **in order**. Each phase has a concrete "done when" test. Do
not proceed to the next phase until the current one's test passes under
`make run`. Commit after each phase.

### Phase 0 — Toolchain sanity
Build and boot a binary that does nothing but spin (`b .`).
**Done when:** QEMU boots it and doesn't crash/reset-loop. Confirm via
`make debug` + `lldb -s debug/lldbinit` that PC is sitting in your infinite
loop.

### Phase 1 — Boot sequence + UART hello world
`start.S`: drop EL2→EL1 (or handle already being in EL1 depending on QEMU's
reset state — check this, don't assume), set `sp_el1`, zero BSS, jump to
`kmain`. `uart.c`: PL011 init + polling `putc`.
**Done when:** `"SILICON SWARM BOOT OK"` prints over serial on every `make run`.

### Phase 2 — Exception vectors
`vectors.S`: full 16-entry `VBAR_EL1` table (sync/IRQ/FIQ/SError ×
EL1t/EL1h/EL0-64/EL0-32). Default handlers should print the exception type +
`ESR_EL1`/`ELR_EL1` over UART and halt — you want loud, informative crashes,
not silent hangs, for everything that follows.
**Done when:** deliberately triggering a data abort (e.g. write to address
`0x0`) prints a readable diagnostic instead of hanging or silently resetting.

### Phase 3 — MMU + caches
Build Stage-1 translation tables (identity-map is fine for v1). Configure
`TTBR0_EL1`, `MAIR_EL1`, `TCR_EL1`. Mark RAM as Normal/Cacheable, mark all MMIO
regions (UART, GIC, fw_cfg, framebuffer once it exists) as Device-nGnRnE
(non-cacheable). Enable MMU + I/D caches via `SCTLR_EL1`.
**Done when:** UART still works with MMU enabled (proves your device-memory
mapping is correct — if you cache MMIO you'll get stale/garbage register
reads), and you can demonstrate a measurable speedup on a tight compute loop
with caches on vs. off.

### Phase 4 — GIC + timer interrupt (60Hz heartbeat)
GICv2 init (distributor + CPU interface), enable IRQ ID 30, configure
`CNTP_TVAL_EL0`/`CNTP_CTL_EL0` for a periodic tick, unmask IRQ in `DAIF`, handle
it in your vector table's IRQ entry, reload `CNTP_TVAL_EL0` in the handler.
**Done when:** a counter incremented only inside the timer IRQ handler reaches
a value corresponding to ~60 ticks/sec measured against wall-clock (print it
over UART periodically to check).

### Phase 5 — Framebuffer via ramfb
Negotiate `ramfb` through `fw_cfg` (select `"etc/ramfb"`, write the ramfb
config struct — address, fourcc format, width, height, stride — via the fw_cfg
DMA interface). **This is the fiddliest hardware-negotiation step in the whole
project; budget real iteration time here.**
**Done when:** `make run-gfx` shows a solid-color fullscreen fill, then a
visible test pattern (gradient or checkerboard) computed pixel-by-pixel.

### Phase 6 — Input (UART polling)
Poll UART RX register in the main loop (or via RX interrupt if you want it
event-driven). Map WASD + a few action keys to game input events.
**Done when:** pressing keys visibly moves a single test sprite/cursor on the
framebuffer.

### Phase 7 — SoA entity storage + bump allocator
Static arrays: `int32_t entity_x[MAX_ENTITIES]`, `entity_y[]`,
`int16_t entity_hp[]`, `uint8_t entity_type[]`, `uint8_t entity_alive[]`. No
per-entity structs. Simple bump allocator for any auxiliary scratch memory (no
free() semantics needed for v1). This is the first module hardware-independent
enough to unit-test with `make test-host` per §0.7 — set that target up here.
**Done when:** you can spawn/update/render 10,000 dummy entities moving in
straight lines at a stable 60Hz, measured via your timer tick counter.

### Phase 8 — Flow field pathfinding
City grid as a 2D gravity/cost map. BFS or wavefront propagation from the city
center (or from active targets) to build a gradient field. Entities read the
gradient at their current cell and step downhill — no per-entity search.
**Done when:** entities spawned at screen edges visibly path around a
wall/obstacle toward the center without any per-entity pathfinding cost (verify
via your cycle counter that update cost stays flat as obstacle count grows).

### Phase 9 — NEON blit + update loop
Vectorize the entity position update and pixel-write loop using NEON (4-8 lanes
per instruction, `q0`-`q31`). Handle stride/alignment so cache-line reads are
sequential (this is *why* you did SoA in Phase 7).
**Done when:** you can push entity count materially higher than Phase 7's
baseline while holding 60Hz, and you can show the before/after cycle-count
delta from vectorizing. Treat "hundreds of thousands" as a stretch target, not
a hard gate — per-frame flowfield + spatial hash + blit cost on a single core
may cap the realistic ceiling lower; don't block progress on hitting an
arbitrary number.

### Phase 10 — Spatial hashing for collision/combat
Grid-bucket entities by position each frame. Resolve overlaps and combat only
within a cell + its neighbors — this is the part the original concept glossed
over, and it's necessary or you'll silently degrade to O(n²) the moment
entities start fighting.
**Done when:** two opposing entity groups can collide and reduce each other's
HP without frame time scaling quadratically as group size grows.

### Phase 11 — Game loop integration
State machine: `BUILD_PHASE` (grid editing, zoning, placing turrets/barricades
via input) → `SIEGE_PHASE` (wave spawning at screen edges, flow field
recalculated toward city center, combat resolution active) → win/loss check →
back to build or game over.
**Done when:** a full build→siege→resolution loop is playable end to end.

### Phase 12 — Performance pass
Use `PMCCNTR_EL0` (cycle counter, needs `PMUSERENR_EL0` access enabled) to
profile hot paths. Check cache-line alignment on all SoA arrays (align to
64B). Confirm HVF is actually engaged and you're not accidentally running under
TCG.
**Done when:** you have a documented entity-count ceiling at stable 60Hz, with
a short writeup of what the actual bottleneck was (memory bandwidth vs. compute
vs. spatial hash overhead).

---

## 7. Architecture Requirements (non-negotiable)

- **SoA, not AoS.** No `struct Entity { x, y, hp }` arrays. Parallel flat arrays
  only.
- **Fixed-point math** for positions/gradients in hot paths — no scalar float,
  no unnecessary FPU/NEON mode transitions.
- **No per-entity heap allocation, ever.** Static arrays sized to
  `MAX_ENTITIES`, with an `alive` flag or freelist for reuse, not malloc/free
  per spawn/death.
- **Cache-line alignment** (64B) on all major SoA arrays.
- **Spatial hashing**, not naive pairwise checks, for anything that scales with
  entity count squared.

---

## 8. Game Design Spec

**Build Phase:**
- Top-down grid canvas, framebuffer-rendered.
- Player places: roads, power (implicit radius or simple grid propagation),
  residential/defensive zones, turrets, barricades.
- Grid state lives in a simple 2D array
  (`uint8_t city_grid[GRID_H][GRID_W]`), tile-type enum per cell.
- Turrets/barricades directly modify the flow field cost map (barricades =
  high cost/impassable, turrets = damage source with a range check against the
  spatial hash).

**Siege Phase:**
- Entities spawn at screen-edge cells in escalating waves.
- Flow field recalculated toward city center (or nearest defended structure —
  your call, state which you pick).
- Entities collide with barricades/turrets/each other via spatial hash; turrets
  deal damage to entities within range each tick.
- Loss condition: entities reach city center / core structure HP hits 0. Win
  condition: wave timer or kill-count threshold survived.

**Controls (UART-polled, v1):**
- WASD — camera pan
- Number keys or similar — select build tool
- Enter/Space — confirm placement / trigger next wave
- Keep this minimal for v1; expand only after Phase 11 is playable.

---

## 9. Agent Working Instructions

- **Work phase by phase.** Do not write Phase 8's flow field code before Phase
  4's timer interrupt is verified working. Bare-metal bugs compound silently —
  an unverified lower layer will produce baffling failures three phases later.
- **Comment every hardware register write** with what the bits mean and why
  (e.g. `// SCTLR_EL1.M=1 (MMU enable), .C=1 (data cache), .I=1 (instr cache)`),
  not just what value you're writing. This code is unreadable without it.
- **Prefer a loud stub over a silent skip.** If a subsystem isn't implemented
  yet, make it print `"NOT YET IMPLEMENTED: <thing>"` over UART and halt, not
  silently no-op.
- **Verify before optimizing.** Don't reach for NEON in Phase 7 — get the
  scalar version correct and measured first, then vectorize in Phase 9 with a
  before/after comparison.
- **Flag any deviation from the memory map in §5** immediately — if `dumpdtb`
  disagrees with the reference table, trust the dump, not this document.
- **Stop and report** if a phase's "done when" test can't be made to pass after
  reasonable effort, rather than silently moving on with a broken foundation.
- **Use `lldb`, not `gdb`, for all debugging** (§0.6) — do not introduce a
  Homebrew `gdb` dependency into this project.
- **Prefer host-side unit tests** for hardware-independent `engine/`/`game/`
  logic (§0.7) over always round-tripping through QEMU + UART.

---

## 10. Stretch Goals (post-v1, do not start early)

- GICv3 support (system-register interface, redistributors) for
  closer-to-real-hardware fidelity.
- Full `virtio-input` keyboard driver (replacing UART polling) for smoother
  continuous input.
- Port the boot/MMU/framebuffer layer to real Raspberry Pi 4 hardware (GPU
  mailbox interface instead of ramfb, different UART base address, real device
  tree instead of QEMU's synthetic one).
- Multi-core via PSCI (`CPU_ON`) — parallelize entity update across cores, which
  would need per-core cache-partition-aware SoA chunking to actually pay off.
