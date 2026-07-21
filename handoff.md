# Handoff

## Goals

Silicon Swarm v2: bare-metal AArch64 (QEMU `virt`) isometric city-builder +
swarm siege — RCT-1999-style 2.5D at 1280×720, hot paths in NEON assembly,
game logic in freestanding C. See [README.md](README.md) for design and
controls.

## Current state (2026-07-21)

**v2 complete and verified end-to-end on a live display.** All six v2
phases built, tested, committed on `main`:

- P0: 1280×720 ramfb (screendump-verified).
- P1: 128×128 corner-heightmap terrain, 2:1 diamond projection, painter's
  order, RCT ±1 slope rule, NEON span fills, slope/height shading.
- P2: tile cursor + q/e terraforming with directional slope propagation.
- P3: city layer — road/house/barricade/turret prisms, money, income only
  for road-adjacent houses, demolish refunds.
- P4: siege ported to world space (flowfield + spatial hash retargeted to
  the world grid; build_phase module deleted). Asymmetric combat ranges so
  turrets actually defend. Verified live: 2000 attackers, SIEGE WON,
  bounty, return to build; loss path shows banner, enter restarts.
- P5: HUD (3×5 bitmap font) — money/tool/core HP/foes + game-over banner.
- P6: perf documented — worst frame ≈ 2.5M cycles (render 1.0–2.2M,
  combat ≤ 0.24M) vs ≥16M cycle 60Hz budget; hot loops already NEON.

Host tests: 6/6 green (`make test-host`): entity_soa, alloc, flowfield,
spatial_hash, terrain (slope invariant, rasterizer bounds, terraform),
city (placement rules, economy, refunds).

## Verification rig (no GUI needed)

`-serial pty` echoes the guest's own log back as input (pty line
discipline) — use
`-serial unix:/tmp/sock,server=on,wait=off -monitor unix:/tmp/mon,server=on,wait=off`,
drive keys through the serial socket, capture frames with the monitor's
`screendump` command.

## Next steps

None required. Polish ideas: 4-way view rotation, entity/building painter
interleaving (entities currently draw after all terrain — visible only
when a swarm passes behind a tall hill), wave escalation across sieges,
water, sound-free win screen art.
