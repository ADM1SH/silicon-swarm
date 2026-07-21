# Handoff

## Goals

Build Silicon Swarm: a bare-metal (no OS, no libc) AArch64 game for QEMU
`virt`, combining a Cities: Skylines-style build phase with an entity-swarm
siege phase, optimized for raw cache/SIMD throughput. Full design decisions,
hard constraints, and the 13-phase roadmap (Phase 0 through Phase 12) live in
[README.md](README.md).

## Current state

**All work is done and verified.** All 12 roadmap phases are implemented,
committed, and pushed to https://github.com/ADM1SH/silicon-swarm (branch
`main`). The engine boots, plays a full build → siege → win/loss loop, and
has been profiled (`PMCCNTR_EL0`).

The previously open graphical-mode bug ("blank, unresponsive window") is
**resolved and verified on a real machine with a display** (2026-07-21):

- **Input** — the earlier `-serial stdio` fix (commit `345e4b8`) works. The
  full loop was driven end-to-end over the serial port against a live
  `-display cocoa` instance: tool selection (`1`/`2`), cursor movement
  (WASD), placement (space, three turrets placed), siege start (enter),
  siege resolution. Every action echoed correctly on the serial log.
- **Rendering** — QEMU `screendump` captures of the live display device
  show the build screen (grid + core + cursor) and a mid-siege frame
  (2000 attackers swarming the core, turret tiles visible) rendering
  correctly through ramfb.
- **Root cause of the "blank window" report** — two compounding UX issues,
  no rendering bug: (1) the initial build frame was near-solid black
  (background + one 16px core tile), easily mistaken for a blank window;
  (2) keystrokes typed into the graphical window go nowhere (input is
  serial-only), making it feel unresponsive. The non-resizable window is
  normal QEMU cocoa behavior for a fixed-size ramfb surface, not a bug.

## Changes made this session (2026-07-21)

- `kernel/kmain.c`: build-phase render now draws a faint cell grid
  (`draw_grid_lines`, `GRID_COLOR`) so the first frame is visibly a play
  field instead of a near-black screen.
- `README.md`: "How to play" now states explicitly that input goes into the
  **terminal** window (serial stdio), not the graphical window.
- Verified `make build` clean and all 5 host unit tests pass
  (`make test-host`).

One verification-rig gotcha worth remembering: testing with `-serial pty`
feeds the guest's own boot log back into its UART as keystrokes (pty line
discipline echo), producing phantom input. Use
`-serial unix:/path,server=on,wait=off` for clean scripted testing;
`-serial stdio` (what `run-gfx` uses) is unaffected.

## Next steps

None required. Optional polish ideas only: sound-free win/lose screen
graphics, a HUD showing selected tool/core HP, difficulty tuning (the
default siege is very hard to win with few turrets).
