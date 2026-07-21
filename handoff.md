# Handoff

## Goals

Build Silicon Swarm: a bare-metal (no OS, no libc) AArch64 game for QEMU
`virt`, combining a Cities: Skylines-style build phase with an entity-swarm
siege phase, optimized for raw cache/SIMD throughput. Full design decisions,
hard constraints, and the 13-phase roadmap (Phase 0 through Phase 12) live in
[README.md](README.md). Immediate goal right now: get the graphical
(`make run-gfx` / `play.command`) window actually playable — see Failed
attempts below, this is not yet confirmed working.

## Current state

All 12 roadmap phases are implemented, committed, and pushed to
https://github.com/ADM1SH/silicon-swarm (branch `main`). The engine boots,
plays a full build → siege → win/loss loop, and has been profiled
(`PMCCNTR_EL0`). Headless mode (`make run`, serial-only) has been verified
extensively and repeatedly throughout development — every phase's "done when"
test passed that way. Graphical mode (`make run-gfx` / `play.command`) is
where the current open problem is: the user reported a blank, unresponsive,
non-resizable window, and a likely root cause has been fixed but **not yet
confirmed** against a real display (this session has no GUI to test with).

## Active files

Full project tree, per [README.md](README.md)'s Project structure section:
- `boot/start.S`, `vectors.S`, `mmu.S` — boot, exceptions, MMU (Phases 0-3)
- `kernel/kmain.c`, `uart.c/.h`, `timer.c/.h`, `gic.c/.h`, `framebuffer.c/.h`,
  `alloc.c/.h`, `perf.c/.h` — subsystem drivers + `PMCCNTR_EL0` (Phase 12)
- `engine/entity_soa.c/.h`, `flowfield.c/.h`, `blit_neon.S/.h`,
  `spatial_hash.c/.h` — SoA entities, pathfinding, NEON, combat broad-phase
- `game/build_phase.c/.h`, `siege_phase.c/.h`, `input.c/.h` — the actual game
- `tests/*.c` — 5 host-side unit tests, run via `make test-host`
- `Makefile`, `linker.ld`, `play.command`, `debug/lldbinit`, `.vscode/`,
  `README.md`, `PROJECT_PROMPT.md`, this file

## Changes made

Phases 0-9 (boot through NEON): standard bring-up, each verified via its own
"done when" test — see README's Roadmap section and the git log (one commit
per phase) for specifics.

Phase 10 (spatial hashing): grid-bucket collision/combat; found and fixed a
real bug along the way (`kernel/alloc.c`'s 1MB arena was too small for the
spatial hash's entity-index array at high `MAX_ENTITIES`, causing a segfault
from an unchecked NULL `bump_alloc()` return — bumped to 16MB).

Phase 11 (game loop): `BUILD_PHASE`/`SIEGE_PHASE`/win-loss state machine.
Verified end-to-end in QEMU (headless): placed turrets, started the siege,
watched it resolve to a loss, state froze correctly afterward.

Phase 12 (performance pass): `PMCCNTR_EL0` cycle counting (had to clear
`MDCR_EL2` in `boot/start.S` so EL1 access isn't trapped), fixed a real
cache-alignment gap (spatial hash / flow field bump-allocated arrays were
4-byte aligned, not 64B), documented the actual bottleneck (spatial-hash
overhead under dense combat, not memory bandwidth or general compute).

Post-roadmap: added `play.command` (double-click launcher) and updated
README to reflect completion (it was still written as a pre-Phase-0 forward
plan). Most recently: **`Makefile`'s `run-gfx` target had no `-serial` flag**
— added `-serial stdio` so keyboard input (this project reads input via PL011
UART polling, not a virtual PS/2 keyboard) actually reaches the guest when
running graphically, matching what `-nographic` already did for `run`.
Committed as `345e4b8`.

## Failed attempts

**Unresolved as of this handoff.** The user reported `make run-gfx` /
`play.command` shows a blank/gray window, wrong-sized, and doesn't respond to
input or resizing.

- First hypothesis: a QEMU cocoa + `ramfb` display-refresh quirk. Suggested
  resizing the window or toggling fullscreen (Cmd+F) to force a redraw — the
  user reported the window can't be resized at all.
- Re-examined `run-gfx`'s QEMU flags and found it had **no `-serial` flag**,
  unlike `run`'s `-nographic` (which implicitly routes stdin/stdout to the
  guest's serial port). Since every input handler in this project
  (`game/input.c`) polls the PL011 UART, not a virtualized keyboard, keystrokes
  typed into the graphical QEMU window would go nowhere — the game would boot,
  render its first frame, and then sit there forever with no way to interact
  with it. That matches the reported symptoms closely (unresponsive, appears
  static) but does not obviously explain "blank/wrong-sized," which may still
  be a separate, unconfirmed cocoa/ramfb rendering issue layered on top.
- Fixed by adding `-serial stdio` to `run-gfx` (commit `345e4b8`). Verified in
  this sandbox (no real display attached) that the boot/game UART log now
  streams through the terminal even with `-display cocoa` specified — before
  this fix it would have gone nowhere silently. **This has not been confirmed
  against a real window on the reporter's machine yet** — this sandbox cannot
  open an actual GUI window to test the visual/cocoa side of the fix.
- Told the user the interaction model changes: type into the **terminal**
  window that launches QEMU (where the log now prints), not the graphical
  window itself. Awaiting confirmation this resolves it.

## Next steps

1. **Confirm the `-serial stdio` fix actually resolves the reported issue** —
   ask the user to relaunch `play.command`, type WASD/1/2/space/enter into the
   *terminal* window (not the graphical one), and check whether the boot log
   appears and the cursor/game responds.
2. If it does not: the blank/wrong-sized window is likely a separate cocoa +
   `ramfb` display bug, independent of input routing. Next debugging steps
   would be checking the QEMU version's known ramfb/cocoa issues, trying
   `-display cocoa,show-cursor=on` or other suboptions, or considering
   `-display none` + a separate screenshot/VNC-based verification path since
   this sandbox has no way to visually confirm a cocoa window's contents
   directly.
3. If it does resolve: consider adding a one-line note to README's "How to
   play" section clarifying that input goes into the terminal window, not the
   graphical one — this is a genuinely non-obvious interaction model worth
   documenting explicitly so it doesn't trip up the next person.
