# 219 — Emisión DMA encadenada por canal (una ristra de 20 balas = 1 canal)


## Búsqueda previa

Evaluado el camino de juego de sprites (`SpriteManager`, `SpriteAllocator`,
`actor_to_sprite_intent`; `OBJECT_SYSTEM.md`) antes de escribir la demo. Se emite a mano con
`sprite_chain.hpp` (`arm_dma_chain`/`build_sprite_dma_chain`) a propósito: el sujeto de la
demo es **la estructura DMA encadenada** (técnica F3) — la capa de actores se apoya en este
camino, cubierto por HOST-434 y por las demos 216/217.

Demo de la técnica **F3** (`docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.2): cuando un canal de sprite sirve **varios objetos en franjas disjuntas** (multiplexado vertical), la emisión puede ser una **estructura DMA encadenada** que Agnus recorre sola, en lugar de un rearme por Copper en cada franja.

La demo mantiene **20 balas** (16×8, 2 planos) en columna por **un solo canal** (el 3) con la columna “respirando” en Y (pistón: todas visibles y en el mismo orden por Y). El **cuadrado superior izquierdo** identifica la estrategia activa en las capturas: COLOR 1 = rearme, 2 = cadena, 3 = persistente.

## Las tres emisiones

| Modo | `SPRITE219_STRATEGY` | Por frame | Estructura |
|---|---|---|---|
| Rearme por Copper | `0` | 1 armado + **19 rearmes** (`WAIT` + `SPRxPOS/CTL/PT` por franja) | La DATA de la bala (sin cabecera) |
| Cadena DMA reconstruida | `1` | `build_sprite_dma_chain` + **un armado** (POS/CTL de la primera franja + PT) | `[POS,CTL,DATA…]×20 + 0,0` en Chip (doble buffer) |
| Cadena persistente | `2` (defecto) | `patch_sprite_chain_block` (4 bytes por bala) + un armado | La cadena se construye **una vez** |

El mecanismo está contrastado con la fuente del emulador: el DMA lee la cabecera `POS/CTL` de `SPRxPT` cuando `dmastate == 0` (`../WinUAE-DBG/custom.cpp:4078-4090`), el canal ocioso sigue consumiendo words (`custom.cpp:10055-10120`) y el puntero avanza con cada fetch (`custom.cpp:12012-12023`); el formato de la estructura y la *columna fantasma* están en `docs/reference/emulators/winuae/sprite-dma.md`. El formato de la cadena y el armado tienen **golden host** en `tests/host/graphics/434_sprite_dma_chain`.

## A/B

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain --debug
bash tools/run/run-demo.sh   demos/techniques/amiga/sprites/219_sprite_dma_chain --keep-running

# variantes del A/B:
EXTRA_DEFINES=-DSPRITE219_STRATEGY=0 bash tools/build/build-demo.sh \
  demos/techniques/amiga/sprites/219_sprite_dma_chain --debug
EXTRA_DEFINES=-DSPRITE219_STRATEGY=1 bash tools/build/build-demo.sh \
  demos/techniques/amiga/sprites/219_sprite_dma_chain --debug

node tools/debug/measure-fps.mjs 219_sprite_dma_chain A500_debug --json
```

Resultados medidos (2026-10-13, A500_debug): los tres modos a **49.92 fps / 1.00159 campos por frame**; la copperlist emitida baja de **304 words** (rearme) a **114** (cadena/persistente). Informe completo con prompts de visión y respuestas crudas: [`VALIDATION.md`](VALIDATION.md).

## Referencias

- `engine/include/eng/graphics/sprite_chain.hpp` (`build_sprite_dma_chain`, `arm_dma_chain`, `patch_sprite_chain_block`, `choose_sprite_emit_plan`).
- `engine/include/eng/graphics/sprite_manager.hpp` (`emit_placements_into` con `chains`).
- HOST-434 (golden de words, armado, selector) y HOST-428 (formato POS/CTL).
- `docs/reference/emulators/winuae/sprite-dma.md`; AHRM 3.ª cap. 4.
