# Handoff

## Goals

Build Silicon Swarm: a bare-metal (no OS, no libc) AArch64 game for QEMU `virt`,
combining a Cities: Skylines-style build phase with a hundred-thousand-entity
swarm siege phase, optimized for raw cache/SIMD throughput. Full design decisions,
hard constraints, and the 13-phase roadmap (Phase 0 through Phase 12) live in
[README.md](README.md).

## Current state

Pre-Phase-0. Environment is set up and verified; zero game code has been written.
Repo is public on GitHub: https://github.com/ADM1SH/silicon-swarm (branch `main`,
one commit).

## Active files

- [README.md](README.md) — full project brief: design decisions, host setup,
  hard constraints, project structure, build system, memory map, phased roadmap,
  working notes.
- [.gitignore](.gitignore) — excludes build outputs (`*.o`, `*.elf`, `*.img`,
  `*.bin`, `*.map`, dtb/dts dumps) and `.DS_Store`.
- [debug/lldbinit](debug/lldbinit) — lldb setup for attaching to QEMU's gdbstub
  (`-s -S`); used instead of gdb to avoid Homebrew gdb's macOS codesigning
  requirement.
- No `Makefile`, `linker.ld`, `boot/`, `kernel/`, `engine/`, or `game/` yet — all
  first appear in Phase 0 onward.

## Changes made

1. Verified the toolchain end to end: `llvm 22.1.8`, `qemu 11.0.2`, `lld 22.1.8`,
   `dtc 1.8.1` all present via Homebrew; confirmed `hvf` is listed in
   `qemu-system-aarch64 -accel help`; confirmed a trivial freestanding object
   compiles with `clang -target aarch64-none-elf -ffreestanding -nostdlib`, links
   with `ld.lld`, and converts with `llvm-objcopy -O binary`.
2. Resolved the debugger gap: chose `lldb` (bundled with Xcode CLT) over `gdb`,
   since Homebrew's `gdb` needs a self-signed codesigning cert to attach to
   processes on macOS. Wrote `debug/lldbinit` with `target create` +
   `gdb-remote localhost:1234`.
3. Documented the native testing gap: `engine/` and `game/` modules are
   hardware-independent freestanding C, so they'll get host-compiled unit tests
   (plain host clang, no `-target aarch64-none-elf`) instead of always
   round-tripping through QEMU + UART. Captured as a README section, no code yet
   since no modules exist.
4. Wrote `README.md`, `.gitignore`, `debug/lldbinit`.
5. `git init`, renamed default branch to `main`, committed all three files.
6. Created public GitHub repo `ADM1SH/silicon-swarm` via `gh repo create
   --public --source=. --remote=origin --push` and pushed the initial commit.

## Failed attempts

None at the project/code level — no implementation has been attempted yet, so
nothing has failed there. One tooling hiccup during setup: an `AskUserQuestion`
call for repo visibility/name was first submitted with only one option in the
second question, which the tool rejected (options arrays need ≥2 entries); retried
immediately with a second option added and succeeded.

## Next steps

Start Phase 0 (toolchain sanity) per the roadmap in README.md:
1. Write `boot/start.S` with a minimal reset handler that just spins (`b .`).
2. Write `linker.ld` placing `.text` at `0x40000000` (the `virt` machine's
   `-kernel` load address).
3. Write a minimal `Makefile` with at least `build`, `run`, `debug`, and `clean`
   targets (per the Build System section of the README).
4. `make run` — confirm QEMU boots without crash/reset-looping.
5. `make debug` + `lldb -s debug/lldbinit` — confirm PC is sitting in the spin
   loop.
6. Commit, then move to Phase 1 (boot sequence + UART hello world) — do not start
   Phase 1 code before Phase 0's done-when test passes.
