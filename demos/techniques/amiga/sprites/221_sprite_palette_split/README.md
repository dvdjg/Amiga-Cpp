# 221 — Palette splitting de sprites (el mismo arte, dos paletas por franja)

Demo de la técnica **F7/F4.3** (`docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.5): dos anillos de 16×16 viven en **un solo canal** (el 0, franjas disjuntas) y cada uno declara su **paleta** (`ActorDesc::sprite_palette`). El compositor arbitra con `plan_sprite_palettes`: el segundo actor **conmuta** los registros de su par al entrar en su franja (**CopperSwitch**) mediante un `SpritePaletteEvent` que `SpriteScene::present` intercala con los rearmes. Es el mismo arte pintado con dos juegos de colores sin gastar planos ni canales (`sprite-layer.md` §3: los canales de un par comparten `COLOR16-19`).

- **Fachada de juego**: alta de actores + `emit` + `present` (el juego no toca canales ni Copper).
- **Paletas**: arrays indexados por registro (`colors[first+i]` = `COLOR[first+i]`); A = cian (paleta base), B = magenta (conmutación por franja). La conmutación **sigue a la franja** porque el evento se emite en el `top` del actor de cada frame.
- **Bucle por señal** (`os::init` + `run_frames`): el latido de VBlank va por la IRQ y el bucle duerme en `Wait()` (patrón 216; el sondeo de VPOSR daba 2 campos/frame en esta demo).

## Medidas (A500_debug)

- `measure-fps.mjs`: **49.92 fps**, 1.00159 campos/frame.

## Validación

Captura directa: anillo superior **cian** (paleta A), inferior **magenta** (paleta B) — misma forma, colores distintos por franja. Detalle en [`VALIDATION.md`](VALIDATION.md).

## Ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/221_sprite_palette_split --debug
bash tools/run/run-demo.sh   demos/techniques/amiga/sprites/221_sprite_palette_split --keep-running
node tools/debug/measure-fps.mjs 221_sprite_palette_split A500_debug --json
```

## Referencias

- `engine/include/eng/graphics/sprite_palette.hpp` (planificador, HOST-433) y `eng/scene/actor_sprite.hpp` (arbitraje en `compose_sprites`).
- HOST-391 (fachada: conmutación y degradación) y HOST-433 (planificador).
- `docs/reference/amiga/techniques/sprite-layer.md` §3-§4; AHRM 3.ª cap. 4.
