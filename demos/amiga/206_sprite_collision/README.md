# 206 — colisión de hardware de sprites (`CLXCON`/`CLXDAT`)

Valida en hardware la colisión **pixel-perfect** de sprites del chipset y su utilidad en el
engine (`graphics/sprite_collision.hpp` + `MinimalBackend::set_sprite_collision`/
`read_sprite_collision`).

## Qué hace

- Compone un display planar 320×256×4 (planos **separados**) y pinta un rectángulo en el
  **bitplane 0** (color 1).
- Coloca un **sprite** sólido de 16×16 encima, en el mismo rectángulo.
- Configura `CLXCON` con el par 0 (sprites 0/1) y **todos los bitplanes deshabilitados**
  (AHRM Table 7-4 NOTE: *«If all bitplanes are excluded, a bitplane collision will always be
  detected»*).
- Cada frame lee `CLXDAT` (que **se autolimpia**) y, si el par 0 colisiona con los bitplanes,
  pone `COLOR00` en **rojo** (si no, navy).

## Cómo verlo

`COLOR00` (fondo) en rojo = colisión detectada; navy = sin colisión. El sprite (verde) queda
sobre el rectángulo (rojo).

```
   bash ./tools/build/build-demo.sh demos/amiga/206_sprite_collision --debug
   bash ./tools/run/run-demo.sh demos/amiga/206_sprite_collision --warp
```

## Referencias

- AHRM 3.ª, Table 7-3 (`CLXDAT`) y Table 7-4 (`CLXCON`).
- `docs/reference/amiga/techniques/sprite-layer.md` §8.
