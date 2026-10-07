# HOST-433 — arbitraje de paleta de Sprites HW (`eng/graphics/sprite_palette.hpp`)

Valida el planificador puro de F7 del `ROADMAP_JUEGO_SPRITES_BOBS.md` §5, con los hechos de
hardware del AHRM cap. 4 y `sprite-layer.md` §3-§4: sin *attached* cada **par** de canales usa
cuatro registros `COLOR16+4·p..+3`; con *attached* el par lee todo `COLOR17..31`; en vertical un
canal reusado conmuta su paleta con un `MOVE` de Copper (`PaletteLine`).

Cubre: conflicto simultáneo (mismos registros en líneas solapadas con paletas distintas) →
`Degrade` del de menor `z` (empate: índice mayor); registros disjuntos → ambos `Ok`; reuso
vertical con paletas distintas → `CopperSwitch` con su `CopperIntent` (línea `top`, `first`,
`count`, paleta) mientras el primero queda `Ok`; misma paleta (mismo puntero) → sin trabajo; sin
requisito de paleta (`count == 0`) → `Ok`; y sin buffer de Copper la decisión se mantiene pero no
se emite la intención.

Ejecución:

```bash
bash tools/run-host-tests.sh tests/host/graphics/433_sprite_palette
```
