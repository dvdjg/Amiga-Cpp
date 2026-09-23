# HOST-042 — `ModeSwitchZone` (conmutación de geometría)

Valida `eng::graphics::ModeSwitchZone` y `eng::copper::Scheduler::emit_mode_switch_zone`
(`engine/include/eng/graphics/mode_switch.hpp`), la pieza de Fase 1b del refactor de
playfields: tramos con **distinta geometría de vídeo** (p. ej. un HUD con menos planos
que el campo).

## Qué fija

- El orden canónico MI09 en un único `WAIT`:
  `BPLCON0 → [BPLCON4] → DDFSTRT/DDFSTOP → BPL1MOD/BPL2MOD → BPLxPT → [paleta]`.
- Que el `DDF` va antes que los módulos y que los punteros van al final (cada plano,
  par PTH/PTL, con `plane_bytes` de stride).
- Las guardas: `DDF` debe ser múltiplo de 4 (`ddf_aligned()`), `planes ≤ 6` y no se
  reapuntan planos sin base; un rechazo **no escribe** palabras.
- Los contadores del `ScheduleReport` (`display_moves`, `palette_moves`, `waits`).

## Cómo corre

```bash
bash tools/run-host-tests.sh tests/host/042_mode_switch
```

## Relación

- Invariante hardware MI09 (`docs/reference/amiga/hardware/amiga-hardware-invariants-microtests.md`):
  este test fija el **orden**; la evidencia en hardware (HUD de 2/3/4 planos bajo un
  split) queda como microtest de demo pendiente.
- Modelo: `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` §4.1.
- Variante conservadora sin cambio de geometría: `CopperIntentKind::BitplaneSplit`.
